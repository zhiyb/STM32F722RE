#pragma once

#include <stdbool.h>
#include <stdint.h>

void usb_init();
void usb_connect(bool enable);
bool usb_is_connected();
void usb_process();

void usb_hw_buf_init();
void usb_hw_ep0_init();
void usb_hw_ep_ctr_irq();
bool usb_hw_act();
void usb_hw_ep_process();
void usb_hw_ep_tx(uint8_t ep, void *data, uint32_t len, bool status_out);

void usb_ep0_setup(void *data, uint32_t len);

const uint8_t *usb_desc_get(uint8_t type, uint8_t index, uint16_t *len);
