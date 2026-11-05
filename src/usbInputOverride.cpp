#include <string.h>
#include "common.h"
#include "hardware/timer.h"
#include "hardware/structs/usb.h"
#include "usbEventQueue.h"
#include "tinyusb/dcd.h"
#include "usbInputOverride.h"
#include "usbInputOverrideDescriptors.h"
#include "powerSaving.h"

static constexpr u8 CDC_REQUEST_SET_LINE_CODING = 0x20;
static constexpr u8 CDC_REQUEST_GET_LINE_CODING = 0x21;
static constexpr u8 CDC_REQUEST_SET_CONTROL_LINE_STATE = 0x22;

static volatile bool sInitialized = false;
static volatile bool sInitRequested = false;
static volatile u32 sInitRequestedAtUs;
static tusb_control_request_t sPendingRequest;
static bool sAwaitingStatusOut = false;
static u8 sLineCodingRxBuffer[7];
static u8 sLineCodingResponse[7];
static bool sLineCodingDataPending = false;
static u8 sReceiveBuffer[2];
static u32 sInputState = 0xFFFF;

static constexpr u16 kEp0MaxPacketSize = 64;
static const u8* sMultiPacketPtr = nullptr;
static u16 sMultiPacketRemaining = 0;

void usbInputOverride_init(void)
{
    usb_setCartridgeIrqEnabled(false);
    pwr_disableUsbPowerSaving();
    dcd_init(0, nullptr);
    dcd_int_enable(0);
    sInitialized = true;
}

// Called from the slot-1 command handlers, so these must not run from flash
void __time_critical_func(usbInputOverride_requestInit)(void)
{
    if (!sInitialized && !sInitRequested)
    {
        sInitRequestedAtUs = time_us_32();
        sInitRequested = true;
    }
}

bool __time_critical_func(usbInputOverride_isInitialized)(void)
{
    return sInitialized;
}

static void resetControlTransferState(void)
{
    memset(&sPendingRequest, 0, sizeof(sPendingRequest));
    sAwaitingStatusOut = false;
    sLineCodingDataPending = false;
    sMultiPacketPtr = nullptr;
    sMultiPacketRemaining = 0;
}

void usbInputOverride_reset(void)
{
    if (sInitialized)
    {
        dcd_deinit(0);
        usb_setCartridgeIrqEnabled(true);
        resetControlTransferState();
        memset(sLineCodingRxBuffer, 0, sizeof(sLineCodingRxBuffer));
        memset(sLineCodingResponse, 0, sizeof(sLineCodingResponse));
        memset(sReceiveBuffer, 0, sizeof(sReceiveBuffer));
        sInputState = 0xFFFF;
        sInitialized = false;
        sInitRequested = false;
    }
}

void usbInputOverride_task(void)
{
    // If the init runs too close the resetNtrCard() from booting a agmethe dspico will crash, this 3 second
    // wait after the first time 0xEC is called should be plenty of time to avoid it.
    if (!sInitialized && sInitRequested && time_us_32() - sInitRequestedAtUs >= 3000000u) {
        usbInputOverride_init();
    }

    while (sInitialized)
    {
        u32 raw = usb_dequeueEvent();
        if (raw == USB_EVENT_NONE)
        {
            break;
        }

        u32 event = raw & 0x7FFFFFFF;
        
        if (event == USB_EVENT_BUS_RESET || event == USB_EVENT_UNPLUGGED)
        {
            resetControlTransferState();
            sInputState = 0xFFFF;
        }
        else if (event == USB_EVENT_SUSPEND)
        {
            // Without VBUS detection an unplugged cable looks like a suspend
            sInputState = 0xFFFF;
        }
        else if (event & USB_EVENT_SETUP_RECEIVED)
        {
            resetControlTransferState();
            u32 event1 = usb_dequeueEvent() & 0x7FFFFFFF;

            // EP0 may still be armed for an earlier request the host gave up on
            usb_dpram->ep_buf_ctrl[0].in &= ~USB_BUF_CTRL_AVAIL;
            usb_dpram->ep_buf_ctrl[0].out &= ~USB_BUF_CTRL_AVAIL;

            sPendingRequest.wIndex = event & 0xFFFF;
            sPendingRequest.wLength = (event >> 16) & 0x1FFF;
            u8 direction = (event >> 29) & 0x1;

            sPendingRequest.wValue = event1 & 0xFFFF;
            sPendingRequest.bRequest = (event1 >> 16) & 0xFF;
            sPendingRequest.bmRequestType = ((event1 >> 24) & 0x7F) | (direction << 7);
            
            switch(sPendingRequest.bRequest) 
            {
                case TUSB_REQ_GET_DESCRIPTOR:
                {
                    u8 descriptorType = (sPendingRequest.wValue >> 8) & 0xFF;
                    const u8* descriptor_ptr = nullptr;
                    u16 descriptor_len = 0;

                    if (descriptorType == 1)
                    {
                        descriptor_ptr = reinterpret_cast<const u8*>(&gDeviceDescriptor);
                        descriptor_len = sizeof(gDeviceDescriptor);
                    }
                    else if (descriptorType == 2)
                    {
                        descriptor_ptr = gConfigDescriptor;
                        descriptor_len = gConfigDescriptorLength;
                    }
                    if (descriptor_ptr != nullptr)
                    {
                        u16 len = TU_MIN(sPendingRequest.wLength, descriptor_len);
                        u16 firstChunk = TU_MIN(len, kEp0MaxPacketSize);

                        dcd_edpt_xfer(0, 0x80, const_cast<u8*>(descriptor_ptr), firstChunk);

                        if (len > firstChunk)
                        {
                            sMultiPacketPtr = descriptor_ptr + firstChunk;
                            sMultiPacketRemaining = len - firstChunk;
                            sAwaitingStatusOut = false; // don't arm status stage until the last chunk is sent
                        }
                        else
                        {
                            sMultiPacketRemaining = 0;
                            sAwaitingStatusOut = true;
                        }
                    }
                    else
                    {
                        dcd_edpt_stall(0, 0x80);
                    }
                    break;
                }
                case TUSB_REQ_SET_ADDRESS:
                {
                    dcd_set_address(0, sPendingRequest.wValue);
                    break;
                }
                case TUSB_REQ_SET_CONFIGURATION:
                {
                    dcd_edpt_close_all(0);
                    if (sPendingRequest.wValue != 0)
                    {
                        // The IN endpoints are never armed, so they always NAK
                        dcd_edpt_open(0, gNotificationEndpointDescriptor);
                        dcd_edpt_open(0, gDataInEndpointDescriptor);
                        dcd_edpt_open(0, gDataOutEndpointDescriptor);
                        dcd_edpt_xfer(0, 0x80, nullptr, 0);

                        dcd_edpt_xfer(0, 0x02, sReceiveBuffer, sizeof(sReceiveBuffer));
                    }
                    else
                    {
                        dcd_edpt_xfer(0, 0x80, nullptr, 0);
                    }
                    break;
                }
                case CDC_REQUEST_SET_CONTROL_LINE_STATE:
                {
                    // DTR is cleared when the client closes the port
                    if ((sPendingRequest.wValue & 1) == 0)
                    {
                        sInputState = 0xFFFF;
                    }
                    dcd_edpt_xfer(0, 0x80, nullptr, 0);
                    break;
                }
                case CDC_REQUEST_SET_LINE_CODING:
                {
                    if (sPendingRequest.wLength != sizeof(sLineCodingRxBuffer))
                    {
                        dcd_edpt_stall(0, 0x00);
                    }
                    else
                    {
                        sLineCodingDataPending = true;
                        dcd_edpt_xfer(0, 0x00, sLineCodingRxBuffer, sizeof(sLineCodingRxBuffer));
                    }
                    break;
                }
                case CDC_REQUEST_GET_LINE_CODING:
                {
                    dcd_edpt_xfer(0, 0x80, sLineCodingResponse, TU_MIN(sPendingRequest.wLength, 7));
                    sAwaitingStatusOut = true;
                    break;
                }
                default:
                {
                    dcd_edpt_stall(0, 0x80);
                }

            }
        }

        else if (event & USB_EVENT_XFER_COMPLETE)
        {
            u8 epAddr = event & 0xFF;
            u32 xferredLen = (event >> 8) & 0x1FFF;

            // EP0 IN
            if (epAddr == 0x80) 
            { 
                if (sMultiPacketRemaining > 0)
                {
                    u16 chunk = TU_MIN(sMultiPacketRemaining, kEp0MaxPacketSize);
                    dcd_edpt_xfer(0, 0x80, const_cast<u8*>(sMultiPacketPtr), chunk);
                    sMultiPacketPtr += chunk;
                    sMultiPacketRemaining -= chunk;
                    if (sMultiPacketRemaining == 0)
                    {
                        sAwaitingStatusOut = true;
                    }
                }
                else if (sAwaitingStatusOut)
                {
                    sAwaitingStatusOut = false;
                    dcd_edpt_xfer(0, 0x00, nullptr, 0);
                }
                else
                {
                    dcd_edpt0_status_complete(0, &sPendingRequest);
                }
            }
            // EP0 OUT
            else if (epAddr == 0x00) 
            {
                if (sLineCodingDataPending)
                {
                    sLineCodingDataPending = false;
                    if (xferredLen == sizeof(sLineCodingRxBuffer))
                    {
                        memcpy(sLineCodingResponse, sLineCodingRxBuffer, sizeof(sLineCodingResponse));
                        dcd_edpt_xfer(0, 0x80, nullptr, 0);
                    }
                    else
                    {
                        dcd_edpt_stall(0, 0x80);
                    }
                }
            }
            // data out
            else if (epAddr == 0x02)
            {
                // A bus reset may have closed the endpoint after this transfer completed
                if (!(usb_dpram->ep_ctrl[1].out & EP_CTRL_ENABLE_BITS))
                {
                    continue;
                }

                if (xferredLen >= 2)
                {
                    sInputState = sReceiveBuffer[0] | (sReceiveBuffer[1] << 8);
                }

                dcd_edpt_xfer(0, 0x02, sReceiveBuffer, sizeof(sReceiveBuffer)); // arm for next send
            }
        }
    }
}

u32 __time_critical_func(usbInputOverride_getState)(void)
{
    return sInputState;
}