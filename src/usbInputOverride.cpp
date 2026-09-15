#include <string.h>
#include "common.h"
#include "usbEventQueue.h"
#include "tinyusb/dcd.h"
#include "usbInputOverride.h"
#include "usbInputOverrideDescriptors.h"

static constexpr u8 CDC_REQUEST_SET_LINE_CODING = 0x20;
static constexpr u8 CDC_REQUEST_GET_LINE_CODING = 0x21;
static constexpr u8 CDC_REQUEST_SET_CONTROL_LINE_STATE = 0x22;

static tusb_control_request_t sPendingRequest;
static u8 sLineCodingRxBuffer[7];
static u8 sLineCodingResponse[7];
static u8 sReceiveBuffer[2];
static u32 sInputState;

void usbInputOverride_init(void){
    sInputState = 0xFFFF;
    dcd_init(0, nullptr);
}    

void usbInputOverride_task(void)
{
    while (true)
    {
        u32 raw = usb_dequeueEvent();
        if (raw == USB_EVENT_NONE)
        {
            break;
        }

        u32 event = raw & 0x7FFFFFFF;
        
        if (event == USB_EVENT_BUS_RESET || event == USB_EVENT_UNPLUGGED)
        {
            memset(&sPendingRequest, 0, sizeof(tusb_control_request_t));
        }
        else if (event & USB_EVENT_SETUP_RECEIVED)
        {
            u32 event1 = usb_dequeueEvent() & 0x7FFFFFFF;

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
                        dcd_edpt_xfer(0, 0x80, const_cast<u8*>(descriptor_ptr), TU_MIN(sPendingRequest.wLength, descriptor_len));
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
                    if (sPendingRequest.wValue != 0)
                    {
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
                    dcd_edpt_xfer(0, 0x80, nullptr, 0);
                    break;
                }
                case CDC_REQUEST_SET_LINE_CODING:
                {
                    dcd_edpt_xfer(0, 0x00, sLineCodingRxBuffer, sPendingRequest.wLength);
                    break;
                }
                case CDC_REQUEST_GET_LINE_CODING:
                {
                    dcd_edpt_xfer(0, 0x80, sLineCodingResponse, TU_MIN(sPendingRequest.wLength, 7));
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
            u32 xferredLen = (event >> 8) & 0xFFFFFF;

            // EP0 IN
            if (epAddr == 0x80) 
            { 
                dcd_edpt0_status_complete(0, &sPendingRequest);
            }
            // EP0 OUT
            else if (epAddr == 0x00) 
            {
                dcd_edpt_xfer(0, 0x80, nullptr, 0);
            }
            // data out
            else if (epAddr == 0x02) 
            {
                if (xferredLen >= 2)
                {
                    sInputState = sReceiveBuffer[0] | (sReceiveBuffer[1] << 8);
                }

                dcd_edpt_xfer(0, 0x02, sReceiveBuffer, sizeof(sReceiveBuffer)); // arm for next send
            }
        }
    }
}

u32 usbInputOverride_getState(void)
{
    return sInputState;
}