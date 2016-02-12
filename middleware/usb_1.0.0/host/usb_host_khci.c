/*
 * Copyright (c) 2015, Freescale Semiconductor, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * o Redistributions of source code must retain the above copyright notice, this list
 *   of conditions and the following disclaimer.
 *
 * o Redistributions in binary form must reproduce the above copyright notice, this
 *   list of conditions and the following disclaimer in the documentation and/or
 *   other materials provided with the distribution.
 *
 * o Neither the name of Freescale Semiconductor, Inc. nor the names of its
 *   contributors may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "usb_host_config.h"
#if ((defined USB_HOST_CONFIG_KHCI) && (USB_HOST_CONFIG_KHCI))
#include "usb_host.h"
#include "usb_host_hci.h"
#include "fsl_device_registers.h"
#include "usb_host_khci.h"
#include "usb_host_devices.h"

/*******************************************************************************
 * Variables
 ******************************************************************************/
#if defined(__ICCCF__) || defined(__ICCARM__)
#pragma segment = "USB_BDT_Z"
#pragma data_alignment = 512
__no_init static uint8_t bdt[512] @"USB_BDT_Z";
#elif defined(__GNUC__)
__attribute__((aligned(512))) static uint8_t bdt[512];
#elif defined(__CC_ARM)
__align(512) uint8_t bdt[512];
#else
#error Unsupported compiler, please use IAR, Keil or arm gcc compiler and rebuild the project.
#endif

/*******************************************************************************
 * Code
 ******************************************************************************/
/*!
 * @brief get the 2 power value of uint32_t.
 *
 * @param data     input uint32_t value.
 *
 */
static uint32_t _USB_HostKhciGetRoundUpPow2(uint32_t data)
{
    uint8_t i = 0U;

    if ((data == 1U) || (data == 0U))
    {
        return data;
    }
    while (data != 1U)
    {
        data = data >> 1U;
        i++;
    }
    return 1U << (i);
}

/*!
 * @brief get the current host khci frame number count.
 *
 * @param handle      Pointer of the host KHCI state structure.
 *
 * @return current frame number count.
 */
static uint32_t _USB_HostKhciGetFrameCount(usb_host_controller_handle handle)
{
    uint32_t tempFrameCount;
    usb_khci_host_state_struct_t *usbHostPointer = (usb_khci_host_state_struct_t *)handle;

    tempFrameCount = usbHostPointer->usbRegBase->FRMNUMH;

    return (uint16_t)((tempFrameCount << 8U) | (usbHostPointer->usbRegBase->FRMNUML));
}

/*!
 * @brief get the total of  host khci frame number count.
 *
 * @param usbHostPointer     Pointer of the host KHCI state structure.
 *
 * @return total of frame number count.
 */
static uint32_t _USB_HostKhciGetFrameCountSum(usb_khci_host_state_struct_t *usbHostPointer)
{
    static uint32_t total_frame_number = 0U;
    static uint16_t old_frame_number = 0U;
    uint16_t frame_number = 0xFFFFU;

    frame_number = _USB_HostKhciGetFrameCount((usb_host_controller_handle)usbHostPointer);

    if (frame_number < old_frame_number)
    {
        total_frame_number += 2048U;
    }

    old_frame_number = frame_number;

    return (frame_number + total_frame_number);
}

/*!
 * @brief host khci delay.
 *
 * @param usbHostPointer     Pointer of the host KHCI state structure.
 * @param ms                 milliseconds.
 *
 */
static void _USB_HostKhciDelay(usb_khci_host_state_struct_t *usbHostPointer, uint32_t ms)
{
    uint32_t sof_start;
    uint32_t sof_end;
    sof_start = _USB_HostKhciGetFrameCountSum(usbHostPointer);

    do
    {
        sof_end = _USB_HostKhciGetFrameCountSum(usbHostPointer);
    } while ((sof_end - sof_start) < ms);
}

/*!
 * @brief Device KHCI isr function.
 *
 * The function is KHCI interrupt service routine.
 *
 * @param hostHandle The host handle.
 */
void USB_HostKhciIsrFunction(void *hostHandle)
{
    usb_khci_host_state_struct_t *usbHostPointer;

    uint8_t status;

    if (hostHandle == NULL)
    {
        return;
    }

    usbHostPointer = (usb_khci_host_state_struct_t *)((usb_host_instance_t *)hostHandle)->controllerHandle;

    while (1U)
    {
        status = (uint8_t)((usbHostPointer->usbRegBase->ISTAT));
        status &= (uint8_t)(usbHostPointer->usbRegBase->INTEN);

        if (!status)
        {
            break;
        }

        usbHostPointer->usbRegBase->ISTAT = status;

        if (status & USB_ISTAT_SOFTOK_MASK)
        {
            USB_OsaEventSet(usbHostPointer->khciEventPointer, USB_KHCI_EVENT_SOF_TOK);
        }

        if (status & USB_ISTAT_ATTACH_MASK)
        {
            usbHostPointer->usbRegBase->INTEN &= (~USB_INTEN_ATTACHEN_MASK);
            USB_OsaEventSet(usbHostPointer->khciEventPointer, USB_KHCI_EVENT_ATTACH);
        }

        if (status & USB_ISTAT_TOKDNE_MASK)
        {
            /* atom transaction done - token done */
            USB_OsaEventSet(usbHostPointer->khciEventPointer, USB_KHCI_EVENT_TOK_DONE);
        }

        if (status & USB_ISTAT_USBRST_MASK)
        {
            USB_OsaEventSet(usbHostPointer->khciEventPointer, USB_KHCI_EVENT_RESET);
        }
    }
}

/*!
 * @brief Handle khci host controller attach event.
 *
 * The function is used to handle attach  event when a device is attached to khci host controller, the process is detect
 * the line state, do bus reset, and call device attached function.
 *
 * @param usbHostPointer           Pointer of the host KHCI state structure.
 *
 */
static void _USB_HostKhciAttach(usb_khci_host_state_struct_t *usbHostPointer)
{
    uint8_t speed;
    uint8_t temp;
    usb_device_handle deviceHandle;
    uint8_t index = 0U;

    usbHostPointer->usbRegBase->CTL |= USB_CTL_ODDRST_MASK;
    usbHostPointer->usbRegBase->CTL = USB_CTL_HOSTMODEEN_MASK;
    usbHostPointer->usbRegBase->ADDR = ((usbHostPointer->usbRegBase->ADDR & (~USB_ADDR_ADDR_MASK)) |
                                        ((((0U) << USB_ADDR_ADDR_SHIFT) & USB_ADDR_ADDR_MASK)));
    usbHostPointer->usbRegBase->ADDR &= (~USB_ADDR_LSEN_MASK);

    /* here wait for about 120ms to check line state */
    for (volatile uint32_t i = 0U; i < 2000000U; i++)
    {
        __ASM("nop");
    }

    do
    {
        temp = ((usbHostPointer->usbRegBase->CTL) & USB_CTL_JSTATE_MASK) ? 0U : 1U;
        for (volatile uint32_t i = 0U; i < 100000U; i++)
        {
            __ASM("nop");
        }
        speed = ((usbHostPointer->usbRegBase->CTL) & USB_CTL_JSTATE_MASK) ? 0U : 1U;
        index++;
    } while ((temp != speed) && (index < USB_KHCI_MAX_SPEED_DETECTION_COUNT));

    if (temp != speed)
    {
#ifdef HOST_ECHO
        usb_echo("speed not match!\n");
#endif
        usbHostPointer->usbRegBase->INTEN |= USB_INTEN_ATTACHEN_MASK;
        return;
    }
    if (((usbHostPointer->usbRegBase->CTL) & USB_CTL_SE0_MASK) == USB_CTL_SE0_MASK)
    {
        usbHostPointer->usbRegBase->INTEN |= USB_INTEN_ATTACHEN_MASK;
        return;
    }

    if (speed == USB_SPEED_FULL)
    {
        usbHostPointer->usbRegBase->ADDR &= (~USB_ADDR_LSEN_MASK);
    }
    else if (speed == USB_SPEED_LOW)
    {
        usbHostPointer->usbRegBase->ENDPOINT[0U].ENDPT = USB_ENDPT_HOSTWOHUB_MASK;
        usbHostPointer->usbRegBase->ADDR |= USB_ADDR_LSEN_MASK;
    }
    else
    {
    }

    usbHostPointer->usbRegBase->CTL |= USB_CTL_USBENSOFEN_MASK;

    usbHostPointer->usbRegBase->ISTAT = 0xffU;
    usbHostPointer->usbRegBase->INTEN &= (~(USB_INTEN_TOKDNEEN_MASK | USB_INTEN_USBRSTEN_MASK));

    /* Do USB bus reset here */
    usbHostPointer->usbRegBase->CTL |= USB_CTL_RESET_MASK;
    _USB_HostKhciDelay(usbHostPointer, 30U);
    usbHostPointer->usbRegBase->CTL &= (~USB_CTL_RESET_MASK);

#ifdef USBCFG_OTG
    _USB_HostKhciDelay(usbHostPointer, 30U);
#else
    _USB_HostKhciDelay(usbHostPointer, 100U);
#endif
    usbHostPointer->usbRegBase->CONTROL &= ~USB_CONTROL_DPPULLUPNONOTG_MASK;
    usbHostPointer->usbRegBase->INTEN |= (USB_INTEN_TOKDNEEN_MASK | USB_INTEN_USBRSTEN_MASK);
    usbHostPointer->deviceAttached++;
    USB_OsaEventClear(usbHostPointer->khciEventPointer, USB_KHCI_EVENT_TOK_DONE);
    USB_HostAttachDevice(usbHostPointer->hostHandle, speed, 0U, 0U, 1, &deviceHandle);

    usbHostPointer->txBd = 0U;
    usbHostPointer->rxBd = 0U;
}

/*!
 * @brief Handle khci host controller bus reset event.
 *
 * The function is used to handle khci host controller bus reset event, reset event also is used for khci detached
 * detecction.
 *
 * @param usbHostPointer           Pointer of the host KHCI state structure.
 *
 */
static void _USB_HostKhciReset(usb_khci_host_state_struct_t *usbHostPointer)
{
    volatile uint32_t i = 0xfffU;
    /* clear attach flag */
    usbHostPointer->usbRegBase->ISTAT = USB_ISTAT_ATTACH_MASK;
    while (i--)
    {
        __ASM("nop");
    }
    /* Test the presence of USB device */
    if ((usbHostPointer->usbRegBase->ISTAT) & USB_ISTAT_ATTACH_MASK)
    {
        /* device attached, so really normal reset was performed */
        usbHostPointer->usbRegBase->INTEN |= USB_ISTAT_USBRST_MASK;
        usbHostPointer->usbRegBase->ADDR = ((usbHostPointer->usbRegBase->ADDR & (~USB_ADDR_ADDR_MASK)) |
                                            ((((0U) << USB_ADDR_ADDR_SHIFT) & USB_ADDR_ADDR_MASK)));
        usbHostPointer->usbRegBase->ENDPOINT[0U].ENDPT |= USB_ENDPT_HOSTWOHUB_MASK;
    }
    else
    {
        /* device was detached,, notify about detach */
        USB_OsaEventSet(usbHostPointer->khciEventPointer, USB_KHCI_EVENT_DETACH);
    }
}

/*!
 * @brief Handle khci host controller bus detach event.
 *
 * @param usbHostPointer           Pointer of the host KHCI state structure.
 *
 */
static void _USB_HostKhciDetach(usb_khci_host_state_struct_t *usbHostPointer)
{
    if (usbHostPointer->deviceAttached > 0)
    {
        usbHostPointer->deviceAttached--;
    }
    else
    {
        return;
    }
    USB_HostDetachDevice(usbHostPointer->hostHandle, 0U, 0U);
    /* Enable USB week pull-downs, useful for detecting detach (effectively bus discharge) */
    usbHostPointer->usbRegBase->USBCTRL |= USB_USBCTRL_PDE_MASK;
    /* Remove suspend state */
    usbHostPointer->usbRegBase->USBCTRL &= (~USB_USBCTRL_SUSP_MASK);

    usbHostPointer->usbRegBase->CTL |= USB_CTL_ODDRST_MASK;

    usbHostPointer->usbRegBase->CTL = USB_CTL_HOSTMODEEN_MASK;

    usbHostPointer->txBd = 0U;
    usbHostPointer->rxBd = 0U;

    usbHostPointer->usbRegBase->ISTAT = 0xffU;
    USB_OsaEventClear(usbHostPointer->khciEventPointer, USB_KHCI_EVENT_MSG);
}

/*!
 * @brief get a right  transfer from periodic and async list.
 *
 * This function return a right transfer for khci atom transfer, the function implemented simple USB transaction
 * dispatch algorithm.
 *
 * @param handle           Pointer of the host khci controller handle.
 * @param transfer      Pointer of pointer of transfer node struct,  will get the a tr quest pointer if operator
 * success, will get NULL pointer if fail.
 *
 */
static void _USB_HostKhciGetRightTrRequest(usb_host_controller_handle handle, usb_host_transfer_t **transfer)
{
    usb_host_transfer_t *temp_transfer;
    usb_host_transfer_t *first_transfer;
    usb_host_transfer_t *prev_transfer;
    usb_khci_host_state_struct_t *usbHostPointer = (usb_khci_host_state_struct_t *)handle;
    uint32_t frame_number;

    if (handle == NULL)
    {
        *transfer = NULL;
        return;
    }

    USB_HostKhciLock();
    /* First check whether periodic list is active, will get transfer from periodic list */
    if (usbHostPointer->periodicListAvtive)
    {
        prev_transfer = temp_transfer = usbHostPointer->periodicListPointer;
        frame_number = _USB_HostKhciGetFrameCount(usbHostPointer);
        /* Will get the transfer if the pipe frame count and current frame count is equal */
        while (temp_transfer != NULL)
        {
            if ((temp_transfer->transferPipe->currentCount != frame_number) &&
                (frame_number % temp_transfer->transferPipe->interval == 0U))
            {
                temp_transfer->transferPipe->currentCount = frame_number;
                *transfer = first_transfer = temp_transfer;
                /* Will move the selected interrupt transfer to end of the periodic list */
                if ((temp_transfer->transferPipe->pipeType == USB_ENDPOINT_INTERRUPT) && (temp_transfer->next != NULL))
                {
                    if (temp_transfer == usbHostPointer->periodicListPointer)
                    {
                        usbHostPointer->periodicListPointer = temp_transfer->next;
                    }
                    else
                    {
                        prev_transfer->next = temp_transfer->next;
                    }
                    while (temp_transfer != NULL)
                    {
                        prev_transfer = temp_transfer;
                        temp_transfer = temp_transfer->next;
                    }
                    prev_transfer->next = first_transfer;
                    first_transfer->next = NULL;
                    USB_OsaEventSet(usbHostPointer->khciEventPointer, USB_KHCI_EVENT_MSG);
                }
                USB_HostKhciUnlock();
                return;
            }
            prev_transfer = temp_transfer;
            temp_transfer = temp_transfer->next;
        }
    }
    /* will get the first transfer from active list if no active transfer in async list */
    if (usbHostPointer->asyncListAvtive)
    {
        first_transfer = temp_transfer = usbHostPointer->asyncListPointer;
        *transfer = first_transfer;

        if (temp_transfer->next != NULL)
        {
            usbHostPointer->asyncListPointer = temp_transfer->next;
        }
        else
        {
            USB_HostKhciUnlock();
            return;
        }
        temp_transfer = temp_transfer->next;
        while (temp_transfer != NULL)
        {
            prev_transfer = temp_transfer;
            temp_transfer = temp_transfer->next;
        }
        prev_transfer->next = first_transfer;
        first_transfer->next = NULL;
    }
    USB_HostKhciUnlock();
}

/*!
 * @brief unlink transfer from periodic and async  khci transfer list.
 *
 * @param handle           Pointer of the host khci controller handle.
 * @param transfer      Pointer of transfer node struct, which will be unlink from transfer list.
 *
 */
static void _USB_HostKhciUnlinkTrRequestFromList(usb_host_controller_handle handle, usb_host_transfer_t *transfer)
{
    usb_host_transfer_t *temptr = NULL;
    usb_host_transfer_t *pretr = NULL;
    usb_khci_host_state_struct_t *usbHostPointer = (usb_khci_host_state_struct_t *)handle;

    if ((handle == NULL) || (transfer == NULL))
    {
        return;
    }

    USB_HostKhciLock();
    if (usbHostPointer->asyncListAvtive == 1U)
    {
        temptr = usbHostPointer->asyncListPointer;
        if (transfer == temptr)
        {
            usbHostPointer->asyncListPointer = temptr->next;
        }
        else
        {
            while (temptr != NULL)
            {
                pretr = temptr;
                temptr = temptr->next;
                if (transfer == temptr)
                {
                    pretr->next = temptr->next;
                    break;
                }
            }
        }
        if (usbHostPointer->asyncListPointer == NULL)
        {
            usbHostPointer->asyncListAvtive = 0U;
        }
    }
    if (usbHostPointer->periodicListAvtive == 1U)
    {
        temptr = usbHostPointer->periodicListPointer;
        if (transfer == temptr)
        {
            usbHostPointer->periodicListPointer = temptr->next;
        }
        else
        {
            while (temptr != NULL)
            {
                pretr = temptr;
                temptr = temptr->next;
                if (transfer == temptr)
                {
                    pretr->next = temptr->next;
                    break;
                }
            }
        }
        if (usbHostPointer->periodicListPointer == NULL)
        {
            usbHostPointer->periodicListAvtive = 0U;
        }
    }
    USB_HostKhciUnlock();
}

/*!
 * @brief link transfer to periodic and async khci transfer list.
 *
 * @param handle           Pointer of the host khci controller handle.
 * @param transfer      Pointer of transfer node struct, which will be link to  transfer list.
 *
 */
static usb_status_t _USB_HostKhciLinkTrRequestToList(usb_host_controller_handle controllerHandle,
                                                     usb_host_transfer_t *transfer)
{
    usb_host_transfer_t *temptransfer;
    usb_khci_host_state_struct_t *usbHostPointer = (usb_khci_host_state_struct_t *)controllerHandle;

    if ((transfer == NULL))
    {
        return kStatus_USB_InvalidParameter;
    }

    USB_HostKhciLock();
    if ((transfer->transferPipe->pipeType == USB_ENDPOINT_ISOCHRONOUS) ||
        (transfer->transferPipe->pipeType == USB_ENDPOINT_INTERRUPT))
    {
        if (usbHostPointer->periodicListAvtive == 0U)
        {
            usbHostPointer->periodicListPointer = transfer;
            transfer->next = NULL;
            usbHostPointer->periodicListAvtive = 1U;
        }
        else
        {
            temptransfer = usbHostPointer->periodicListPointer;
            while (temptransfer->next != NULL)
            {
                temptransfer = temptransfer->next;
            }
            temptransfer->next = transfer;
            transfer->next = NULL;
        }
    }
    else if ((transfer->transferPipe->pipeType == USB_ENDPOINT_CONTROL) ||
             (transfer->transferPipe->pipeType == USB_ENDPOINT_BULK))
    {
        if (usbHostPointer->asyncListAvtive == 0U)
        {
            usbHostPointer->asyncListPointer = transfer;
            transfer->next = NULL;
            usbHostPointer->asyncListAvtive = 1U;
        }
        else
        {
            temptransfer = usbHostPointer->asyncListPointer;
            while (temptransfer->next != NULL)
            {
                temptransfer = temptransfer->next;
            }
            temptransfer->next = transfer;
            transfer->next = NULL;
        }
    }
    else
    {
    }
    USB_HostKhciUnlock();
    return kStatus_USB_Success;
}

/*!
 * @brief khci process tranfer callback function.
 *
 * @param controllerHandle           Pointer of the host khci controller handle.
 * @param transfer                      Pointer of transfer , which will be process callback.
 * @param err                              The return value of transfer.
 *
 */
static void _USB_HostKhciProcessTrCallback(usb_host_controller_handle controllerHandle,
                                           usb_host_transfer_t *transfer,
                                           int32_t err)
{
    usb_status_t status = kStatus_USB_Success;

    if (err == USB_KHCI_ATOM_TR_STALL)
    {
        status = kStatus_USB_TransferStall;
    }
    else if ((err == USB_KHCI_ATOM_TR_NAK) || (err >= 0))
    {
        status = kStatus_USB_Success;

        if (err == USB_KHCI_ATOM_TR_NAK)
        {
            status = kStatus_USB_TransferFailed;
        }
    }
    else if (err < 0)
    {
        status = kStatus_USB_TransferFailed;
    }
    else
    {
    }

    transfer->callbackFn(transfer->callbackParam, transfer, status);
}

/*!
 * @brief khci transaction done process function.
 *
 * @param usbHostPointer         Pointer of the host khci controller handle.
 * @param transfer               Pointer of transfer node struct, which will be handled process done.
 *1400
 */
static int32_t _USB_HostKhciTransactionDone(usb_khci_host_state_struct_t *usbHostPointer, usb_host_transfer_t *transfer)
{
    uint32_t bd;
    uint8_t err;
    int32_t transferResult = 0U;
    uint32_t type = kTr_Unknown;
    uint32_t *bdPointer = NULL;
    usb_host_pipe_t *pipeDescPointer = transfer->transferPipe;

    if (pipeDescPointer->pipeType == USB_ENDPOINT_CONTROL)
    {
        if (transfer->setupStatus == kTransfer_Setup0)
        {
            type = kTr_Ctrl;
        }
        else if ((transfer->setupStatus == kTransfer_Setup1))
        {
            if (transfer->transferLength)
            {
                if (transfer->direction == USB_IN)
                {
                    type = kTr_In;
                }
                else
                {
                    type = kTr_Out;
                }
            }
            else
            {
                type = kTr_In;
            }
        }
        else if (transfer->setupStatus == kTransfer_Setup2)
        {
            if (transfer->transferLength)
            {
                if (transfer->direction == USB_IN)
                {
                    type = kTr_Out;
                }
                else
                {
                    type = kTr_In;
                }
            }
            else
            {
                type = kTr_In;
            }
        }
        else if (transfer->setupStatus == kTransfer_Setup3)
        {
            type = kTr_In;
        }
        else
        {
        }
    }
    else
    {
        if (pipeDescPointer->direction == USB_IN)
        {
            type = kTr_In;
        }
        else if (pipeDescPointer->direction == USB_OUT)
        {
            type = kTr_Out;
        }
        else
        {
        }
    }
    switch (type)
    {
        case kTr_Ctrl:
        case kTr_Out:
            usbHostPointer->txBd ^= 1U;
            bdPointer = (uint32_t *)USB_KHCI_BD_PTR(0U, 1, usbHostPointer->txBd);
            usbHostPointer->txBd ^= 1U;
            break;

        case kTr_In:
            usbHostPointer->rxBd ^= 1U;
            bdPointer = (uint32_t *)USB_KHCI_BD_PTR(0U, 0U, usbHostPointer->rxBd);
            usbHostPointer->rxBd ^= 1U;
            break;

        default:
            bdPointer = NULL;
            break;
    }

    if (bdPointer == NULL)
    {
        return -1;
    }

    bd = *bdPointer;
    err = usbHostPointer->usbRegBase->ERRSTAT;
    if (err & (USB_ERRSTAT_PIDERR_MASK | USB_ERRSTAT_CRC5EOF_MASK | USB_ERRSTAT_CRC16_MASK | USB_ERRSTAT_DFN8_MASK |
               USB_ERRSTAT_DMAERR_MASK | USB_ERRSTAT_BTSERR_MASK))
    {
        transferResult = -(int32_t)err;
        return transferResult;
    }
    else
    {
        if (bd & USB_KHCI_BD_OWN)
        {
#ifdef HOST_ECHO
            usb_echo("Own bit is not clear 0x%x\n", (unsigned int)bd);
#endif
            *bdPointer = 0U;
        }
        if ((pipeDescPointer->pipeType == USB_ENDPOINT_ISOCHRONOUS))
        {
            transferResult = (bd >> 16) & 0x3ffU;
        }
        else
        {
            switch (bd >> 2 & 0xfU)
            {
                case 0x03: /* Last Transfer status is DATA0 */
                case 0x0b: /* Last Transfer status is  DATA1 */
                case 0x02: /* Last Transfer status is  ACK   */
                    transferResult = (bd >> 16) & 0x3ffU;
                    /* switch data toggle */
                    pipeDescPointer->nextdata01 ^= 1U;
                    break;

                case 0x0e: /* Last Transfer status is STALL */
                    transferResult = USB_KHCI_ATOM_TR_STALL;
                    break;

                case 0x0a: /* Last Transfer status is NAK */
                    transferResult = USB_KHCI_ATOM_TR_NAK;
                    break;

                case 0x00: /* Last Transfer status is bus timeout **/
                    transferResult = USB_KHCI_ATOM_TR_BUS_TIMEOUT;
                    break;

                case 0x0f: /* Last Transfer status is data error */
                    transferResult = USB_KHCI_ATOM_TR_DATA_ERROR;
                    break;
                default:
                    break;
            }
        }
    }

    if ((kTr_In == type) && (0 == usbHostPointer->sXferSts.isDmaAlign))
    {
        usbHostPointer->sXferSts.isDmaAlign = 1U;
        if (transferResult > 0)
        {
            for (int j = 0; j < transferResult; j++)
            {
                usbHostPointer->sXferSts.rxBufOrig[j] = usbHostPointer->sXferSts.rxBuf[j];
            }
        }
    }
    return transferResult;
}

/*!
 * @brief  khci atom transaction process function.
 *
 * @param usbHostPointer     Pointer of the host khci controller instance.
 * @param type                    The USB transfer type.
 * @param pipeDescPointer    Pointer of usb pipe desc.
 * @param bufPointer            The memory address is needed to be transferred.
 * @param len                      Transferred data length.
 *
 * @return 0 mean sucess or other opertor failure error code.
 *
 */
static int32_t _USB_HostKhciAtomNonblockingTransaction(usb_khci_host_state_struct_t *usbHostPointer,
                                                       uint32_t type,
                                                       usb_host_pipe_t *pipeDescPointer,
                                                       uint8_t *bufPointer,
                                                       uint32_t len)
{
    uint32_t *bdPointer = NULL;
    uint8_t *buf = bufPointer;
    int32_t transferResult;
    uint32_t speed;
    uint32_t address;
    uint32_t level;
    uint8_t counter = 0U;
    uint32_t event_bit;
    usb_osa_status_t osa_status;
    uint8_t ep_ctl_val;

    len = (len > pipeDescPointer->maxPacketSize) ? pipeDescPointer->maxPacketSize : len;
    USB_HostHelperGetPeripheralInformation(pipeDescPointer->deviceHandle, kUSB_HostGetDeviceLevel, &level);
    USB_HostHelperGetPeripheralInformation(pipeDescPointer->deviceHandle, kUSB_HostGetDeviceSpeed, &speed);
    USB_HostHelperGetPeripheralInformation(pipeDescPointer->deviceHandle, kUSB_HostGetDeviceAddress, &address);

    if (speed == USB_SPEED_LOW)
    {
        usbHostPointer->usbRegBase->ADDR |= USB_ADDR_LSEN_MASK;
    }
    else
    {
        usbHostPointer->usbRegBase->ADDR &= (~USB_ADDR_LSEN_MASK);
    }
    usbHostPointer->usbRegBase->ADDR = ((usbHostPointer->usbRegBase->ADDR & (~USB_ADDR_ADDR_MASK)) |
                                        ((((address) << USB_ADDR_ADDR_SHIFT) & USB_ADDR_ADDR_MASK)));

#if (FSL_FEATURE_USB_KHCI_HOST_ENABLED)
    ep_ctl_val = (level == 1 ? USB_ENDPT_HOSTWOHUB_MASK : 0U) | USB_ENDPT_RETRYDIS_MASK | USB_ENDPT_EPTXEN_MASK |
                 USB_ENDPT_EPRXEN_MASK |
                 ((pipeDescPointer->pipeType == USB_ENDPOINT_ISOCHRONOUS ? 1 : USB_ENDPT_EPHSHK_MASK));
#else
    ep_ctl_val = USB_ENDPT_EPTXEN_MASK | USB_ENDPT_EPRXEN_MASK |
                 ((pipeDescPointer->pipeType == USB_ENDPOINT_ISOCHRONOUS ? 1 : USB_ENDPT_EPHSHK_MASK));
#endif
    usbHostPointer->usbRegBase->ENDPOINT[0U].ENDPT = ep_ctl_val;

    transferResult = 0U;
    counter = 0U;
    /* wait for USB conttoller is ready, and with timeout */
    while ((usbHostPointer->usbRegBase->CTL) & USB_CTL_TXSUSPENDTOKENBUSY_MASK)
    {
        _USB_HostKhciDelay(usbHostPointer, 1U);
        osa_status = USB_OsaEventWait(usbHostPointer->khciEventPointer, USB_KHCI_EVENT_TOK_DONE, 0U, 1, &event_bit);
        if (osa_status == kStatus_USB_OSA_Success)
        {
            transferResult = USB_KHCI_ATOM_TR_RESET;
            break;
        }
        else
        {
            counter++;
            if (counter >= 3)
            {
                transferResult = USB_KHCI_ATOM_TR_CRC_ERROR;
                return transferResult;
            }
        }
    }

    if (!transferResult)
    {
#if defined(FSL_FEATURE_USB_KHCI_DYNAMIC_SOF_THRESHOLD_COMPARE_ENABLED) && \
    (FSL_FEATURE_USB_KHCI_DYNAMIC_SOF_THRESHOLD_COMPARE_ENABLED == 1U)
        if (speed == USB_SPEED_LOW)
        {
            usbHostPointer->usbRegBase->SOFTHLD = (len * 12 * 7 / 6 + KHCICFG_THSLD_DELAY) / 8;
        }
        else
        {
            usbHostPointer->usbRegBase->SOFTHLD = (len * 7 / 6 + KHCICFG_THSLD_DELAY) / 8;
        }
#endif
        usbHostPointer->usbRegBase->ERRSTAT = 0xffU;

        if ((kTr_In == type) && ((len & USB_MEM4_ALIGN_MASK) || ((uint32_t)bufPointer & USB_MEM4_ALIGN_MASK)))
        {
            if ((usbHostPointer->khciSwapBufPointer != NULL) && (len <= USB_HOST_CONFIG_KHCI_DMA_ALIGN_BUFFER))
            {
                buf = (uint8_t *)USB_MEM4_ALIGN((uint32_t)(usbHostPointer->khciSwapBufPointer + 4));
                usbHostPointer->sXferSts.rxBuf = buf;
                usbHostPointer->sXferSts.rxBufOrig = bufPointer;
                usbHostPointer->sXferSts.rxLen = len;
                usbHostPointer->sXferSts.isDmaAlign = 0U;
            }
        }
        else
        {
            usbHostPointer->sXferSts.isDmaAlign = 1U;
        }

        switch (type)
        {
            case kTr_Ctrl:
                bdPointer = (uint32_t *)USB_KHCI_BD_PTR(0U, 1, usbHostPointer->txBd);
                *(bdPointer + 1U) = USB_LONG_TO_LITTLE_ENDIAN((uint32_t)buf);
                *bdPointer = USB_LONG_TO_LITTLE_ENDIAN(USB_KHCI_BD_BC(len) | USB_KHCI_BD_OWN);
                usbHostPointer->usbRegBase->TOKEN =
                    (USB_TOKEN_TOKENENDPT((uint8_t)pipeDescPointer->endpointAddress) | USB_TOKEN_TOKENPID(0xD));
                usbHostPointer->txBd ^= 1U;
                break;
            case kTr_In:
                bdPointer = (uint32_t *)USB_KHCI_BD_PTR(0U, 0U, usbHostPointer->rxBd);
                *(bdPointer + 1U) = USB_LONG_TO_LITTLE_ENDIAN((uint32_t)buf);
                *bdPointer = USB_LONG_TO_LITTLE_ENDIAN(USB_KHCI_BD_BC(len) | USB_KHCI_BD_OWN |
                                                       USB_KHCI_BD_DATA01(pipeDescPointer->nextdata01));
                usbHostPointer->usbRegBase->TOKEN =
                    (USB_TOKEN_TOKENENDPT((uint8_t)pipeDescPointer->endpointAddress) | USB_TOKEN_TOKENPID(0x9));
                usbHostPointer->rxBd ^= 1U;
                break;
            case kTr_Out:
                bdPointer = (uint32_t *)USB_KHCI_BD_PTR(0U, 1, usbHostPointer->txBd);
                *(bdPointer + 1U) = USB_LONG_TO_LITTLE_ENDIAN((uint32_t)buf);
                *bdPointer = USB_LONG_TO_LITTLE_ENDIAN(USB_KHCI_BD_BC(len) | USB_KHCI_BD_OWN |
                                                       USB_KHCI_BD_DATA01(pipeDescPointer->nextdata01));
                usbHostPointer->usbRegBase->TOKEN =
                    (USB_TOKEN_TOKENENDPT((uint8_t)pipeDescPointer->endpointAddress) | USB_TOKEN_TOKENPID(0x1));
                usbHostPointer->txBd ^= 1U;
                break;
            default:
                bdPointer = NULL;
                break;
        }
    }

    return transferResult;
}

/*!
 * @brief khci host start tramsfer.
 *
 * @param handle           Pointer of the host khci controller handle.
 * @param transfer      Pointer of transfer node struct, which will transfer.
 *
 * @retval kKhci_TrTransmiting           khci host transaction prime successfully, will enter next stage.
 * @retval kKhci_TrTransmitDone       khci host transaction prime unsuccessfully, will enter exit stage.
 *
 */
static khci_tr_state_t _USB_HostKhciStartTranfer(usb_host_controller_handle handle, usb_host_transfer_t *transfer)
{
    static int32_t transferResult;
    uint8_t *buf;
    usb_khci_host_state_struct_t *usbHostPointer = (usb_khci_host_state_struct_t *)handle;

    if (transfer->transferPipe->pipeType == USB_ENDPOINT_CONTROL)
    {
        if ((transfer->setupStatus == kTransfer_Setup0))
        {
            transferResult = _USB_HostKhciAtomNonblockingTransaction(usbHostPointer, kTr_Ctrl, transfer->transferPipe,
                                                                     (uint8_t *)&transfer->setupPacket, 8U);
        }
        else if (transfer->setupStatus == kTransfer_Setup1)
        {
            if (transfer->transferLength)
            {
                buf = transfer->transferBuffer;
                buf += transfer->transferSofar;
                transferResult = _USB_HostKhciAtomNonblockingTransaction(
                    usbHostPointer, (transfer->direction == USB_IN) ? kTr_In : kTr_Out, transfer->transferPipe, buf,
                    transfer->transferLength - transfer->transferSofar);
            }
            else
            {
                transfer->transferPipe->nextdata01 = 1U;
                transfer->setupStatus = kTransfer_Setup3;
                transferResult =
                    _USB_HostKhciAtomNonblockingTransaction(usbHostPointer, kTr_In, transfer->transferPipe, 0U, 0U);
            }
        }
        else if (transfer->setupStatus == kTransfer_Setup2)
        {
            if (transfer->transferLength)
            {
                transfer->transferPipe->nextdata01 = 1U;

                transferResult = _USB_HostKhciAtomNonblockingTransaction(
                    usbHostPointer, (transfer->direction == USB_IN) ? kTr_Out : kTr_In, transfer->transferPipe, 0U, 0U);
            }
            else
            {
                transfer->transferPipe->nextdata01 = 1U;
                transferResult =
                    _USB_HostKhciAtomNonblockingTransaction(usbHostPointer, kTr_In, transfer->transferPipe, 0U, 0U);
            }
        }
        else if (transfer->setupStatus == kTransfer_Setup3)
        {
            transfer->transferPipe->nextdata01 = 1U;
            transferResult =
                _USB_HostKhciAtomNonblockingTransaction(usbHostPointer, kTr_In, transfer->transferPipe, 0U, 0U);
        }
        else
        {
        }
    }
    else
    {
        buf = transfer->transferBuffer;
        buf += transfer->transferSofar;
        transferResult = _USB_HostKhciAtomNonblockingTransaction(
            usbHostPointer, (transfer->transferPipe->direction == USB_IN) ? kTr_In : kTr_Out, transfer->transferPipe,
            buf, transfer->transferLength - transfer->transferSofar);
    }

    transfer->transferResult = transferResult;

    if (transfer->transferResult == 0U)
    {
        usbHostPointer->trState = kKhci_TrTransmiting;
    }
    else
    {
        usbHostPointer->trState = kKhci_TrTransmitDone;
    }
    return (khci_tr_state_t)usbHostPointer->trState;
}

/*!
 * @brief khci host finish tramsfer.
 *
 * @param handle           Pointer of the host khci controller handle.
 * @param transfer      Pointer of transfer node struct, which will be transfer.
 *
 * @retval kKhci_TrGetMsg                  The current of transaction is transfer done, will enter first stage.
 * @retval kKhci_TrTransmitDone       All of khci host transaction of the transfer have transfer done, will enter exit
 * stage.
 *
 */
static khci_tr_state_t _USB_HostKhciFinishTranfer(usb_host_controller_handle handle, usb_host_transfer_t *transfer)
{
    static int32_t transferResult;
    usb_khci_host_state_struct_t *usbHostPointer = (usb_khci_host_state_struct_t *)handle;

    transfer->transferResult = transferResult = _USB_HostKhciTransactionDone(usbHostPointer, transfer);
    if (transferResult >= 0)
    {
        if (transfer->transferPipe->pipeType == USB_ENDPOINT_CONTROL)
        {
            if ((transfer->setupStatus == kTransfer_Setup2) || (transfer->setupStatus == kTransfer_Setup3))
            {
                usbHostPointer->trState = kKhci_TrTransmitDone;
            }
            else
            {
                usbHostPointer->trState = kKhci_TrStartTransmit;
                if (transfer->setupStatus == kTransfer_Setup1)
                {
                    transfer->transferSofar += transferResult;
                    if (((transfer->transferLength - transfer->transferSofar) <= 0U) ||
                        (transferResult < transfer->transferPipe->maxPacketSize))
                    {
                        transfer->setupStatus++;
                    }
                }
                else
                {
                    transfer->setupStatus++;
                }
            }
        }
        else
        {
            transfer->transferSofar += transferResult;
            if (((transfer->transferLength - transfer->transferSofar) == 0U) ||
                (transferResult < transfer->transferPipe->maxPacketSize))
            {
                usbHostPointer->trState = kKhci_TrTransmitDone;
            }
            else
            {
                usbHostPointer->trState = kKhci_TrStartTransmit;
            }
        }
    }
    else
    {
        if ((transferResult == USB_KHCI_ATOM_TR_NAK))
        {
            if (transfer->transferPipe->pipeType == USB_ENDPOINT_INTERRUPT)
            {
                usbHostPointer->trState = kKhci_TrGetMsg;
                USB_OsaEventSet(usbHostPointer->khciEventPointer, USB_KHCI_EVENT_MSG);
            }
            else
            {
                if ((_USB_HostKhciGetFrameCountSum(usbHostPointer) - transfer->frame) > transfer->nakTimeout)
                {
                    usbHostPointer->trState = kKhci_TrTransmitDone;
                    transfer->transferResult = USB_KHCI_ATOM_TR_BUS_TIMEOUT;
                }
                else
                {
                    usbHostPointer->trState = kKhci_TrGetMsg;
                    USB_OsaEventSet(usbHostPointer->khciEventPointer, USB_KHCI_EVENT_MSG);
                }
            }
        }
        else
        {
            usbHostPointer->trState = kKhci_TrTransmitDone;
        }
    }
    return (khci_tr_state_t)usbHostPointer->trState;
}

/*!
 * @brief  host khci controller transfer clear up
 *
 * The function is used to handle controller transfer clear up.
 * @param handle         Pointer of the host khci controller handle.
 *
 *
 */
void _USB_HostKhciTransferClearUp(usb_host_controller_handle controllerHandle)
{
    usb_khci_host_state_struct_t *usbHostPointer = (usb_khci_host_state_struct_t *)controllerHandle;
    usb_host_transfer_t *trCancel;

    USB_HostKhciLock();
    trCancel = usbHostPointer->periodicListPointer;
    USB_HostKhciUnlock();
    while (trCancel != NULL)
    {
        _USB_HostKhciUnlinkTrRequestFromList(controllerHandle, trCancel);
        trCancel->callbackFn(trCancel->callbackParam, trCancel, kStatus_USB_TransferCancel);
        USB_HostKhciLock();
        trCancel = usbHostPointer->periodicListPointer;
        USB_HostKhciUnlock();
    }

    USB_HostKhciLock();
    trCancel = usbHostPointer->asyncListPointer;
    USB_HostKhciUnlock();
    while (trCancel != NULL)
    {
        _USB_HostKhciUnlinkTrRequestFromList(controllerHandle, trCancel);
        trCancel->callbackFn(trCancel->callbackParam, trCancel, kStatus_USB_TransferCancel);
        USB_HostKhciLock();
        trCancel = usbHostPointer->asyncListPointer;
        USB_HostKhciUnlock();
    }
    usbHostPointer->trState = kKhci_TrGetMsg;
}

/*!
 * @brief  host khci controller transfer state machine
 *
 * The function is used to handle controller transfer state machine.
 * @param handle         Pointer of the host khci controller handle.
 * @param transfer       Pointer of transfer node struct, which will be transfer.
 *
 *
 */
void _USB_HostKhciTransferStateMachine(usb_host_controller_handle controllerHandle, usb_host_transfer_t **ptransfer)
{
    usb_khci_host_state_struct_t *usbHostPointer = (usb_khci_host_state_struct_t *)controllerHandle;
    usb_host_transfer_t *transfer = *ptransfer;

    switch (usbHostPointer->trState)
    {
        case kKhci_TrGetMsg:
            transfer = NULL;
            _USB_HostKhciGetRightTrRequest(controllerHandle, &transfer);
            if (transfer != NULL)
            {
                *ptransfer = transfer;
                usbHostPointer->trState = _USB_HostKhciStartTranfer(controllerHandle, transfer);
                usbHostPointer->trState = kKhci_TrTransmiting;
            }
            break;

        case kKhci_TrStartTransmit:
            if (transfer != NULL)
            {
                usbHostPointer->trState = _USB_HostKhciStartTranfer(controllerHandle, transfer);
            }
            break;

        case kKhci_TrTransmiting:
            if (transfer != NULL)
            {
                if ((_USB_HostKhciGetFrameCountSum(usbHostPointer) - transfer->frame) > USB_TIMEOUT_OTHER)
                {
                    if ((transfer->transferPipe->pipeType == USB_ENDPOINT_CONTROL) ||
                        (transfer->transferPipe->pipeType == USB_ENDPOINT_BULK))
                    {
                        /* clear current bdt status */
                        _USB_HostKhciTransactionDone(usbHostPointer, transfer);
                        usbHostPointer->trState = kKhci_TrTransmitDone;
                        transfer->transferResult = USB_KHCI_ATOM_TR_BUS_TIMEOUT;
                        return;
                    }
                }
            }
            break;

        case kKhci_TrTransmitDone:
            if (transfer != NULL)
            {
                _USB_HostKhciUnlinkTrRequestFromList(usbHostPointer, transfer);
                _USB_HostKhciProcessTrCallback(usbHostPointer, transfer, transfer->transferResult);
                usbHostPointer->trState = kKhci_TrGetMsg;
                if ((usbHostPointer->asyncListAvtive == 1U) || (usbHostPointer->periodicListAvtive == 1U))
                {
                    USB_OsaEventSet(usbHostPointer->khciEventPointer, USB_KHCI_EVENT_MSG);
                }
            }
            break;

        default:
            break;
    }
}

/*!
 * @brief khci task function.
 *
 * The function is used to handle KHCI controller message.
 * In the BM environment, this function should be called periodically in the main function.
 * And in the RTOS environment, this function should be used as a function entry to create a task.
 *
 * @param hostHandle The host handle.
 */
void USB_HostKhciTaskFunction(void *hostHandle)
{
    volatile ptr_usb_host_khci_state_struct_t usbHostPointer;
    uint32_t event_bit;
    static usb_host_transfer_t *transfer;

    if (hostHandle == NULL)
    {
        return;
    }

    usbHostPointer = (usb_khci_host_state_struct_t *)(((usb_host_instance_t *)hostHandle)->controllerHandle);
    if (USB_OsaEventWait(usbHostPointer->khciEventPointer, 0xff, 0U, 1U, &event_bit) ==
        kStatus_USB_OSA_Success) /* wait all event */
    {
        if (event_bit & USB_KHCI_EVENT_ATTACH)
        {
            _USB_HostKhciAttach(usbHostPointer);
            usbHostPointer->trState = kKhci_TrGetMsg;
        }
        if (event_bit & USB_KHCI_EVENT_RESET)
        {
            _USB_HostKhciReset(usbHostPointer);
        }
        if (event_bit & USB_KHCI_EVENT_DETACH)
        {
            _USB_HostKhciDetach(usbHostPointer);
        }
        if (event_bit & USB_KHCI_EVENT_TOK_DONE)
        {
            if (transfer != NULL)
            {
                usbHostPointer->trState =
                    _USB_HostKhciFinishTranfer(((usb_host_instance_t *)hostHandle)->controllerHandle, transfer);
            }
        }
    }
    if (usbHostPointer->deviceAttached)
    {
        _USB_HostKhciTransferStateMachine(usbHostPointer, &transfer);
    }
    else
    {
        _USB_HostKhciTransferClearUp(usbHostPointer);
    }
}

/*!
 * @brief create the USB host khci instance.
 *
 * This function initializes the USB host khci controller driver.
 *
 * @param controllerId        The controller id of the USB IP. Please refer to the enumeration usb_controller_index_t.
 * @param hostHandle         The host level handle.
 * @param controllerHandle  Return the controller instance handle.
 *
 * @retval kStatus_USB_Success              The host is initialized successfully.
 * @retval kStatus_USB_AllocFail             allocate memory fail.
 * @retval kStatus_USB_Error                 host mutex create fail, KHCI/EHCI mutex or KHCI/EHCI event create fail.
 *                                                         Or, KHCI/EHCI IP initialize fail.
 *
 */
usb_status_t USB_HostKhciCreate(uint8_t controllerId,
                                usb_host_handle hostHandle,
                                usb_host_controller_handle *controllerHandle)
{
    usb_khci_host_state_struct_t *usbHostPointer;
    usb_status_t status = kStatus_USB_Success;
    usb_osa_status_t osa_status;
    uint32_t usb_base_addrs[] = USB_BASE_ADDRS;

    if (((controllerId - kUSB_ControllerKhci0) >= (uint8_t)USB_HOST_CONFIG_KHCI) ||
        ((controllerId - kUSB_ControllerKhci0) >= (sizeof(usb_base_addrs) / sizeof(uint32_t))))
    {
        return kStatus_USB_ControllerNotFound;
    }

    usbHostPointer = (usb_khci_host_state_struct_t *)USB_OsaMemoryAllocate(sizeof(usb_khci_host_state_struct_t));
    if (NULL == usbHostPointer)
    {
        *controllerHandle = NULL;
        return kStatus_USB_AllocFail;
    }

    /* Allocate the USB Host Pipe Descriptors */
    usbHostPointer->pipeDescriptorBasePointer = NULL;
    usbHostPointer->hostHandle = hostHandle;
    *controllerHandle = (usb_host_handle)usbHostPointer;

    if (NULL == (usbHostPointer->khciSwapBufPointer =
                     (uint8_t *)USB_OsaMemoryAllocate(USB_HOST_CONFIG_KHCI_DMA_ALIGN_BUFFER + 4)))
    {
#ifdef HOST_DEBUG_
        usb_echo("usbHostPointer->khciSwapBufPointer- memory allocation failed");
#endif
        USB_HostKhciDestory(controllerHandle);
        return kStatus_USB_AllocFail;
    }

    /* init khci mutext */
    osa_status = USB_OsaMutexCreate(&usbHostPointer->khciMutex);
    if (osa_status != kStatus_USB_OSA_Success)
    {
#ifdef HOST_ECHO
        usb_echo("khci mutex init fail\r\n");
#endif
        USB_HostKhciDestory(controllerHandle);
        return kStatus_USB_Error;
    }

    USB_OsaEventCreate(&usbHostPointer->khciEventPointer, 1U);
    if (usbHostPointer->khciEventPointer == NULL)
    {
#ifdef HOST_ECHO
        usb_echo(" memalloc failed in usb_khci_init\n");
#endif

        return kStatus_USB_AllocFail;
    } /* Endif */

    usbHostPointer->usbRegBase = (USB_Type *)usb_base_addrs[controllerId - kUSB_ControllerKhci0];

    usbHostPointer->asyncListAvtive = 0U;
    usbHostPointer->periodicListAvtive = 0U;
    usbHostPointer->periodicListPointer = NULL;
    usbHostPointer->asyncListPointer = NULL;
    usbHostPointer->sXferSts.isDmaAlign = 0U;

    /* set internal register pull down */
    usbHostPointer->usbRegBase->CTL = USB_CTL_SE0_MASK;

    /* Reset USB CTRL register */
    usbHostPointer->usbRegBase->CTL = 0UL;
    usbHostPointer->usbRegBase->ISTAT = 0xffU;
    /* Enable week pull-downs, useful for detecting detach (effectively bus discharge) */
    usbHostPointer->usbRegBase->USBCTRL |= USB_USBCTRL_PDE_MASK;
    /* Remove suspend state */
    usbHostPointer->usbRegBase->USBCTRL &= (~USB_USBCTRL_SUSP_MASK);
    usbHostPointer->usbRegBase->CTL |= USB_CTL_ODDRST_MASK;

    usbHostPointer->usbRegBase->BDTPAGE1 = (uint8_t)((uint32_t)USB_KHCI_BDT_BASE >> 8U);
    usbHostPointer->usbRegBase->BDTPAGE2 = (uint8_t)((uint32_t)USB_KHCI_BDT_BASE >> 16);
    usbHostPointer->usbRegBase->BDTPAGE3 = (uint8_t)((uint32_t)USB_KHCI_BDT_BASE >> 24);
    /* Set SOF threshold */
    usbHostPointer->usbRegBase->SOFTHLD = 255;

    usbHostPointer->usbRegBase->CTL = USB_CTL_HOSTMODEEN_MASK;
    /* Wait for attach interrupt */
    usbHostPointer->usbRegBase->INTEN |= (USB_INTEN_ATTACHEN_MASK | USB_INTEN_SOFTOKEN_MASK);
#if defined(FSL_FEATURE_USB_KHCI_DYNAMIC_SOF_THRESHOLD_COMPARE_ENABLED) && \
    (FSL_FEATURE_USB_KHCI_DYNAMIC_SOF_THRESHOLD_COMPARE_ENABLED == 1U)
    usbHostPointer->usbRegBase->MISCCTRL |= USB_MISCCTRL_SOFDYNTHLD_MASK;
#endif
    usbHostPointer->trState = kKhci_TrGetMsg;
    return status;
}

/*!
 * @brief destroy USB host khci instance.
 *
 * This function de-initialize the USB host khci controller driver.
 *
 * @param handle                                    the controller handle.
 *
 * @retval kStatus_USB_Success              The host is initialized successfully.
 */
usb_status_t USB_HostKhciDestory(usb_host_controller_handle controllerHandle)
{
    usb_khci_host_state_struct_t *usbHostPointer = (usb_khci_host_state_struct_t *)controllerHandle;

    usbHostPointer->usbRegBase->INTEN &= (~0xFFU);

    usbHostPointer->usbRegBase->ADDR = ((usbHostPointer->usbRegBase->ADDR & (~USB_ADDR_ADDR_MASK)) |
                                        ((((0U) << USB_ADDR_ADDR_SHIFT) & USB_ADDR_ADDR_MASK)));
    usbHostPointer->usbRegBase->CTL &= (~0xFFu);
    usbHostPointer->usbRegBase->USBCTRL |= USB_USBCTRL_PDE_MASK;
    usbHostPointer->usbRegBase->USBCTRL |= USB_USBCTRL_SUSP_MASK;
    usbHostPointer->usbRegBase->ADDR &= (~USB_ADDR_LSEN_MASK);

    if (NULL != usbHostPointer->khciEventPointer)
    {
        USB_OsaEventDestroy(usbHostPointer->khciEventPointer);
    }
    if (NULL != usbHostPointer->khciMutex)
    {
        USB_OsaMutexDestroy(usbHostPointer->khciMutex);
    }

    if (NULL != usbHostPointer->khciSwapBufPointer)
    {
        USB_OsaMemoryFree(usbHostPointer->khciSwapBufPointer);
        usbHostPointer->khciSwapBufPointer = NULL;
    }

    USB_OsaMemoryFree(usbHostPointer);
    usbHostPointer = NULL;

    return kStatus_USB_Success;
}

/*!
 * @brief open USB host pipe.
 *
 * This function open one pipe according to the pipeInitPointer parameter.
 *
 * @param controllerHandle  the controller handle.
 * @param pipeHandlePointer    the pipe handle pointer, it is used to return the pipe handle.
 * @param pipeInitPointer         it is used to initialize the pipe.
 *
 * @retval kStatus_USB_Success           The host is initialized successfully.
 * @retval kStatus_USB_Error                there is no idle pipe.
*
*/
usb_status_t USB_HostKhciOpenPipe(usb_host_controller_handle controllerHandle,
                                  usb_host_pipe_handle *pipeHandlePointer,
                                  usb_host_pipe_init_t *pipeInitPointer)
{
    usb_khci_host_state_struct_t *usbHostPointer = (usb_khci_host_state_struct_t *)controllerHandle;
    usb_host_pipe_t *pipePointer;
    usb_host_pipe_t *prePipePointer;
    usb_host_pipe_t *tempPipePointer;

    USB_OSA_SR_ALLOC();
    USB_OSA_ENTER_CRITICAL();
    pipePointer = (usb_host_pipe_t *)USB_OsaMemoryAllocate(sizeof(usb_host_pipe_t));
    if (pipePointer == NULL)
    {
        USB_OsaMemoryFree(usbHostPointer);
        return kStatus_USB_AllocFail;
    }
    else
    {
        if (usbHostPointer->pipeDescriptorBasePointer == NULL)
        {
            usbHostPointer->pipeDescriptorBasePointer = pipePointer;
        }
        else
        {
            tempPipePointer = usbHostPointer->pipeDescriptorBasePointer;
            while (NULL != tempPipePointer)
            {
                prePipePointer = tempPipePointer;
                tempPipePointer = tempPipePointer->next;
            }
            prePipePointer->next = pipePointer;
        }
        pipePointer->next = NULL;
    }
    USB_OSA_EXIT_CRITICAL();

    pipePointer->deviceHandle = pipeInitPointer->devInstance;
    pipePointer->endpointAddress = pipeInitPointer->endpointAddress;
    pipePointer->direction = pipeInitPointer->direction;
    pipePointer->interval = pipeInitPointer->interval;
    pipePointer->maxPacketSize = pipeInitPointer->maxPacketSize;
    pipePointer->pipeType = pipeInitPointer->pipeType;
    pipePointer->numberPerUframe = pipeInitPointer->numberPerUframe;
    pipePointer->nakCount = pipeInitPointer->nakCount;
    pipePointer->nextdata01 = 0U;
    pipePointer->open = (uint8_t)1U;
    pipePointer->currentCount = 0xffffU;

    if (pipePointer->pipeType == USB_ENDPOINT_ISOCHRONOUS)
    {
        pipePointer->interval = 1 << (pipeInitPointer->interval - 1U);
    }
    else
    {
        pipePointer->interval = _USB_HostKhciGetRoundUpPow2(pipeInitPointer->interval);
    }
    *pipeHandlePointer = pipePointer;

    return kStatus_USB_Success;
}

/*!
 * @brief close USB host pipe.
 *
 * This function close one pipe and release the related resources.
 *
 * @param controllerHandle  the controller handle.
 * @param pipeHandle         the closing pipe handle.
 *
 * @retval kStatus_USB_Success              The host is initialized successfully.
 */
usb_status_t USB_HostKhciClosePipe(usb_host_controller_handle controllerHandle, usb_host_pipe_handle pipeHandle)
{
    usb_khci_host_state_struct_t *usbHostPointer = (usb_khci_host_state_struct_t *)controllerHandle;
    usb_host_pipe_t *pipePointer = (usb_host_pipe_t *)pipeHandle;
    usb_host_pipe_t *prePipePointer;

    USB_OSA_SR_ALLOC();
    USB_OSA_ENTER_CRITICAL();

    if ((pipePointer != NULL) && (pipePointer->open == (uint8_t)1U))
    {
        if (pipeHandle == usbHostPointer->pipeDescriptorBasePointer)
        {
            usbHostPointer->pipeDescriptorBasePointer = usbHostPointer->pipeDescriptorBasePointer->next;
            USB_OsaMemoryFree(pipeHandle);
        }
        else
        {
            pipePointer = usbHostPointer->pipeDescriptorBasePointer;
            prePipePointer = pipePointer;
            while (NULL != pipePointer)
            {
                if ((pipePointer->open) && (pipePointer == pipeHandle))
                {
                    prePipePointer->next = pipePointer->next;
                    if (NULL != pipePointer)
                    {
                        USB_OsaMemoryFree(pipePointer);
                        pipePointer = NULL;
                    }
                    break;
                }
                prePipePointer = pipePointer;
                pipePointer = pipePointer->next;
            }
        }
    }
    else
    {
#ifdef HOST_ECHO
        usb_echo("usb_khci_close_pipe invalid pipe \n");
#endif
    }
    USB_OSA_EXIT_CRITICAL();

    return kStatus_USB_Success;
}

/*!
 * @brief send data to pipe.
 *
 * This function request to send the transfer to the specified pipe.
 *
 * @param controllerHandle  the controller handle.
 * @param pipeHandle         the sending pipe handle.
 * @param transfer               the transfer which will be wrote.
 *
 * @retval kStatus_USB_Success              send successfully.
 * @retval kStatus_USB_LackSwapBuffer       there is no swap buffer for KHCI.
 *
 */
usb_status_t USB_HostKhciWritePipe(usb_host_controller_handle controllerHandle,
                                   usb_host_pipe_handle pipeHandle,
                                   usb_host_transfer_t *transfer)
{
    usb_status_t status = kStatus_USB_Success;
    usb_khci_host_state_struct_t *usbHostPointer = (usb_khci_host_state_struct_t *)controllerHandle;

    usb_host_pipe_t *pipePointer = (usb_host_pipe_t *)pipeHandle;

    transfer->transferPipe = pipePointer;
    transfer->retry = RETRY_TIME;

    if (pipePointer->endpointAddress == 0U)
    {
        if ((transfer->direction == USB_IN) && (transfer->transferBuffer != NULL) &&
            ((transfer->transferLength & USB_MEM4_ALIGN_MASK) ||
             ((uint32_t)transfer->transferBuffer & USB_MEM4_ALIGN_MASK)))
        {
            if (usbHostPointer->khciSwapBufPointer == NULL)
            {
                return kStatus_USB_LackSwapBuffer;
            }
            if (pipePointer->maxPacketSize > USB_HOST_CONFIG_KHCI_DMA_ALIGN_BUFFER)
            {
                return kStatus_USB_LackSwapBuffer;
            }
        }
        transfer->setupStatus = kTransfer_Setup0;

        if (transfer->transferLength)
        {
            if (transfer->direction == USB_IN)
            {
                transfer->nakTimeout = USB_TIMEOUT_TOHOST;
            }
            else
            {
                transfer->nakTimeout = USB_TIMEOUT_TODEVICE;
            }
        }
        else
        {
            transfer->nakTimeout = USB_TIMEOUT_NODATA;
        }
    }
    else
    {
        if (pipePointer->nakCount == 0U)
        {
            transfer->nakTimeout = USB_TIMEOUT_DEFAULT;
        }
        else
        {
            transfer->nakTimeout = pipePointer->nakCount * NAK_RETRY_TIME;
        }
    }
    transfer->frame = _USB_HostKhciGetFrameCountSum(usbHostPointer);

    _USB_HostKhciLinkTrRequestToList(controllerHandle, transfer);

    USB_OsaEventSet(usbHostPointer->khciEventPointer, USB_KHCI_EVENT_MSG);

    return status;
}

/*!
 * @brief receive data from pipe.
 *
 * This function request to receive the transfer from the specified pipe.
 *
 * @param controllerHandle the controller handle.
 * @param pipeHandle        the receiving pipe handle.
 * @param transfer             the transfer which will be read.
 *
 * @retval kStatus_USB_Success              send successfully.
 * @retval kStatus_USB_LackSwapBuffer       there is no swap buffer for KHCI.
 *
 */
usb_status_t USB_HostKhciReadpipe(usb_host_controller_handle controllerHandle,
                                  usb_host_pipe_handle pipeHandle,
                                  usb_host_transfer_t *transfer)
{
    usb_status_t status = kStatus_USB_Success;
    usb_khci_host_state_struct_t *usbHostPointer = (usb_khci_host_state_struct_t *)controllerHandle;
    usb_host_pipe_t *pipePointer = (usb_host_pipe_t *)pipeHandle;

    if ((transfer->transferLength & USB_MEM4_ALIGN_MASK) || ((uint32_t)transfer->transferBuffer & USB_MEM4_ALIGN_MASK))
    {
        if (usbHostPointer->khciSwapBufPointer == NULL)
        {
            return kStatus_USB_LackSwapBuffer;
        }
        if (pipePointer->maxPacketSize > USB_HOST_CONFIG_KHCI_DMA_ALIGN_BUFFER)
        {
            return kStatus_USB_LackSwapBuffer;
        }
    }

    transfer->transferPipe = pipePointer;
    transfer->transferSofar = 0U;
    if (pipePointer->nakCount == 0U)
    {
        transfer->nakTimeout = USB_TIMEOUT_DEFAULT;
    }
    else
    {
        transfer->nakTimeout = pipePointer->nakCount * NAK_RETRY_TIME;
    }
    transfer->retry = RETRY_TIME;
    transfer->frame = _USB_HostKhciGetFrameCountSum(usbHostPointer);

    _USB_HostKhciLinkTrRequestToList(controllerHandle, transfer);
    USB_OsaEventSet(usbHostPointer->khciEventPointer, USB_KHCI_EVENT_MSG);

    return status;
}

/*!
 * @brief cancel pipe's transfers.
 *
 * @param handle        Pointer of the host khci controller handle.
 * @param pipePointer      Pointer of the pipe.
 * @param trPointer          The canceling transfer.
 *
 * @return kStatus_USB_Success or error codes.
 */
static usb_status_t _USB_HostKhciCancelPipe(usb_host_controller_handle handle,
                                            usb_host_pipe_t *pipePointer,
                                            usb_host_transfer_t *trPointer)
{
    usb_host_transfer_t *temptr = NULL;
    usb_khci_host_state_struct_t *usbHostPointer = (usb_khci_host_state_struct_t *)handle;

    if ((pipePointer->pipeType == USB_ENDPOINT_ISOCHRONOUS) || (pipePointer->pipeType == USB_ENDPOINT_INTERRUPT))
    {
        temptr = usbHostPointer->periodicListPointer;
    }
    else if ((pipePointer->pipeType == USB_ENDPOINT_CONTROL) || (pipePointer->pipeType == USB_ENDPOINT_BULK))
    {
        temptr = usbHostPointer->asyncListPointer;
    }
    else
    {
    }

    while (temptr != NULL)
    {
        if (((usb_host_pipe_t *)(temptr->transferPipe) == pipePointer) &&
            ((trPointer == NULL) || (trPointer == temptr)))
        {
            _USB_HostKhciUnlinkTrRequestFromList(handle, temptr);
            temptr->callbackFn(temptr->callbackParam, temptr, kStatus_USB_TransferCancel);
            return kStatus_USB_Success;
        }
        temptr = temptr->next;
    }

    return kStatus_USB_Success;
}

/*!
 * @brief  khci bus control.
 *
 * @param handle         Pointer of the host khci controller handle.
 * @param busControl   Bus control code.
 *
 * @return kStatus_USB_Success
 */
static usb_status_t _USB_HostKhciBusControl(usb_host_controller_handle handle, uint8_t busControl)
{
    ptr_usb_host_khci_state_struct_t usbHostPointer = (usb_khci_host_state_struct_t *)handle;
    if (busControl == kUSB_HostBusReset)
    {
        usbHostPointer->usbRegBase->CTL |= USB_CTL_RESET_MASK;
        /* wait for 30 milliseconds (2.5 is minimum for reset, 10 recommended) */
        _USB_HostKhciDelay(usbHostPointer, 30U);
        usbHostPointer->usbRegBase->CTL &= (~USB_CTL_RESET_MASK);
        usbHostPointer->usbRegBase->CTL |= USB_CTL_ODDRST_MASK;
        usbHostPointer->usbRegBase->CTL = USB_CTL_HOSTMODEEN_MASK;

        usbHostPointer->txBd = 0U;
        usbHostPointer->rxBd = 0U;
    }
    else if (busControl == kUSB_HostBusRestart)
    {
        usbHostPointer->deviceAttached = 0U;

        usbHostPointer->usbRegBase->CTL = USB_CTL_HOSTMODEEN_MASK;
        usbHostPointer->usbRegBase->ISTAT = 0xffU;
        /* Now, enable only USB interrupt attach for host mode */
        usbHostPointer->usbRegBase->INTEN |= USB_INTEN_ATTACHEN_MASK;
    }
    else if (busControl == kUSB_HostBusEnableAttach)
    {
        if (usbHostPointer->deviceAttached <= 0)
        {
            usbHostPointer->usbRegBase->INTEN |= USB_INTEN_ATTACHEN_MASK;
        }
    }
    else if (busControl == kUSB_HostBusDisableAttach)
    {
        usbHostPointer->usbRegBase->INTEN &= (~USB_INTEN_ATTACHEN_MASK);
    }
    else
    {
    }

    return kStatus_USB_Success;
}

/*!
 * @brief io control khci.
 *
 * This function implemented khci io control khci.
 *
 * @param controllerHandle  the controller handle.
 * @param ioctlEvent          please reference to enumeration host_busControl_t.
 * @param ioctlParam         the control parameter.
 *
 * @retval kStatus_USB_Success                io ctrol successfully.
 * @retval kStatus_USB_InvalidHandle        The controllerHandle is a NULL pointer.
 */
usb_status_t USB_HostKciIoctl(usb_host_controller_handle controllerHandle, uint32_t ioctlEvent, void *ioctlParam)
{
    usb_status_t status = kStatus_USB_Success;
    usb_host_cancel_param_t *param;

    if (controllerHandle == NULL)
    {
        return kStatus_USB_InvalidHandle;
    }

    switch (ioctlEvent)
    {
        case kUSB_HostCancelTransfer:
            param = (usb_host_cancel_param_t *)ioctlParam;
            status = _USB_HostKhciCancelPipe(controllerHandle, (usb_host_pipe_t *)param->pipeHandle, param->transfer);
            break;

        case kUSB_HostBusControl:
            status = _USB_HostKhciBusControl(controllerHandle, *((uint8_t *)ioctlParam));
            break;

        case kUSB_HostGetFrameNumber:
            *((uint32_t *)ioctlParam) = _USB_HostKhciGetFrameCount(controllerHandle);
            break;

        default:
            break;
    }
    return status;
}
#endif /* USB_HOST_CONFIG_KHCI */