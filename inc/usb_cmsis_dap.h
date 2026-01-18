#pragma once

#include "usb.h"

void usb_cmsis_dap_init();

void usb_cmsis_dap_ep_init(usb_if_t usb_if);
void usb_cmsis_dap_ep_out(usb_if_t usb_if);
void usb_cmsis_dap_ep_in(usb_if_t usb_if);
