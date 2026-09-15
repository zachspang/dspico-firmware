#pragma once
#include "common.h"
#include "tinyusb/dcd.h"

extern const tusb_desc_device_t gDeviceDescriptor;

extern const u8* const gConfigDescriptor;
extern const u16 gConfigDescriptorLength;
extern const tusb_desc_endpoint_t* const gDataOutEndpointDescriptor;