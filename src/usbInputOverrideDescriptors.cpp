#include "common.h"
#include "usbInputOverrideDescriptors.h"

struct __attribute__((packed)) CdcHeaderFunctionalDescriptor
{
    u8  bFunctionLength = 5;
    u8  bDescriptorType = 0x24;
    u8  bDescriptorSubType = 0x00;
    u16 bcdCDC = 0x0110;
};

struct __attribute__((packed)) CdcCallManagementFunctionalDescriptor
{
    u8 bFunctionLength = 5;
    u8 bDescriptorType = 0x24;
    u8 bDescriptorSubType = 0x01;
    u8 bmCapabilities = 0x00;
    u8 bDataInterface = 1;
};

struct __attribute__((packed)) CdcAcmFunctionalDescriptor
{
    u8 bFunctionLength = 4;
    u8 bDescriptorType = 0x24;
    u8 bDescriptorSubType = 0x02;
    u8 bmCapabilities = 0x02; // supports Set/Get_Line_Coding, Set_Control_Line_State
};

struct __attribute__((packed)) CdcUnionFunctionalDescriptor
{
    u8 bFunctionLength = 5;
    u8 bDescriptorType = 0x24;
    u8 bDescriptorSubType = 0x06;
    u8 bMasterInterface = 0;
    u8 bSlaveInterface0 = 1;
};

const tusb_desc_device_t gDeviceDescriptor = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,

    .bDeviceClass       = TUSB_CLASS_CDC,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,

    .bMaxPacketSize0    = 64,

    .idVendor           = 0xCAFE,
    .idProduct          = 0x6000,
    .bcdDevice          = 0x0100,

    .iManufacturer      = 0,
    .iProduct           = 0,
    .iSerialNumber      = 0,

    .bNumConfigurations = 1
};

struct __attribute__((packed)) UsbConfigDescriptorBlob
{
    tusb_desc_configuration_t configHeader;
    tusb_desc_interface_assoc_t iad;

    tusb_desc_interface_t cdcControlInterface;
    CdcHeaderFunctionalDescriptor cdcHeader;
    CdcCallManagementFunctionalDescriptor cdcCallManagement;
    CdcAcmFunctionalDescriptor cdcAcm;
    CdcUnionFunctionalDescriptor cdcUnion;
    tusb_desc_endpoint_t notificationEndpoint;

    tusb_desc_interface_t cdcDataInterface;
    tusb_desc_endpoint_t dataOutEndpoint;
    tusb_desc_endpoint_t dataInEndpoint;
};

static const UsbConfigDescriptorBlob sConfigDescriptorBlob = {
    .configHeader = {
        .bLength             = sizeof(tusb_desc_configuration_t),
        .bDescriptorType     = TUSB_DESC_CONFIGURATION,
        .wTotalLength        = sizeof(UsbConfigDescriptorBlob),
        .bNumInterfaces      = 2,
        .bConfigurationValue = 1,
        .iConfiguration      = 0,
        .bmAttributes        = 0x80,
        .bMaxPower           = 50
    },

    .iad = {
        .bLength            = sizeof(tusb_desc_interface_assoc_t),
        .bDescriptorType    = TUSB_DESC_INTERFACE_ASSOCIATION,
        .bFirstInterface    = 0,
        .bInterfaceCount    = 2,
        .bFunctionClass     = TUSB_CLASS_CDC,
        .bFunctionSubClass  = 0x02,
        .bFunctionProtocol  = 0x00,
        .iFunction          = 0
    },

    .cdcControlInterface = {
        .bLength            = sizeof(tusb_desc_interface_t),
        .bDescriptorType    = TUSB_DESC_INTERFACE,
        .bInterfaceNumber   = 0,
        .bAlternateSetting  = 0,
        .bNumEndpoints      = 1,
        .bInterfaceClass    = TUSB_CLASS_CDC,
        .bInterfaceSubClass = 0x02,
        .bInterfaceProtocol = 0x00,
        .iInterface         = 0
    },

    .cdcHeader = {},
    .cdcCallManagement = {},
    .cdcAcm = {},
    .cdcUnion = {},

    .notificationEndpoint = {
        .bLength          = sizeof(tusb_desc_endpoint_t),
        .bDescriptorType  = TUSB_DESC_ENDPOINT,
        .bEndpointAddress = 0x81, // EP1 IN
        .bmAttributes     = { .xfer = TUSB_XFER_INTERRUPT, .sync = 0, .usage = 0 },
        .wMaxPacketSize   = 8,
        .bInterval        = 16
    },

    .cdcDataInterface = {
        .bLength            = sizeof(tusb_desc_interface_t),
        .bDescriptorType    = TUSB_DESC_INTERFACE,
        .bInterfaceNumber   = 1,
        .bAlternateSetting  = 0,
        .bNumEndpoints      = 2,
        .bInterfaceClass    = 0x0A, // CDC Data
        .bInterfaceSubClass = 0x00,
        .bInterfaceProtocol = 0x00,
        .iInterface         = 0
    },

    .dataOutEndpoint = {
        .bLength          = sizeof(tusb_desc_endpoint_t),
        .bDescriptorType  = TUSB_DESC_ENDPOINT,
        .bEndpointAddress = 0x02, // EP2 OUT - controller-state packets land here
        .bmAttributes     = { .xfer = TUSB_XFER_BULK, .sync = 0, .usage = 0 },
        .wMaxPacketSize   = 64,
        .bInterval        = 0
    },

    .dataInEndpoint = {
        .bLength          = sizeof(tusb_desc_endpoint_t),
        .bDescriptorType  = TUSB_DESC_ENDPOINT,
        .bEndpointAddress = 0x82, // EP2 IN — required by CDC spec, unused by this feature
        .bmAttributes     = { .xfer = TUSB_XFER_BULK, .sync = 0, .usage = 0 },
        .wMaxPacketSize   = 64,
        .bInterval        = 0
    }
};

const u8* const gConfigDescriptor = reinterpret_cast<const u8*>(&sConfigDescriptorBlob);
const u16 gConfigDescriptorLength = sizeof(sConfigDescriptorBlob);
const tusb_desc_endpoint_t* const gDataOutEndpointDescriptor = &sConfigDescriptorBlob.dataOutEndpoint;