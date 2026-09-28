#pragma once
#include "common.h"

void usbInputOverride_init(void);
void usbInputOverride_requestInit(void);
bool usbInputOverride_isInitialized(void);
// Should only be called by ntrc_resetUsb as it only resets 
// the state of things ntrc_resetUsb doesnt
void usbInputOverride_reset(void);
void usbInputOverride_task(void);
u32 usbInputOverride_getState(void);