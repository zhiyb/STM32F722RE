#pragma once

#include <stdbool.h>

void usb_init();
void usb_connect(bool enable);
bool usb_is_connected();
