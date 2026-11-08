#pragma once
#include "common.h"

// Software IRQ that runs usbInputOverride_init()
#define USB_INPUT_OVERRIDE_INIT_IRQ FIRST_USER_IRQ

void usbInputOverride_init(void);
bool usbInputOverride_isInitialized(void);
// Should only be called by ntrc_resetUsb as it only resets 
// the state of things ntrc_resetUsb doesnt
void usbInputOverride_reset(void);
void usbInputOverride_task(void);
u32 usbInputOverride_getState(void);