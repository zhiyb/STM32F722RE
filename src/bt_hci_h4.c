#include "bt_hci_h4.h"
#include "bt_hci_usb.h"
#include "uart.h"
#include "semihosting.h"

#define FIFO_BUF_SIZE   512

static struct {
    uint8_t event[2][256];
    uint8_t rptr, rofs, rlen;
    enum {EventPacket, EventHeader, EventParams} state;
} bt_u4_fifo;

void bt_hci_h4_reset()
{
#if 0
    uart_tx(0x01);
    uart_tx(0x03);
    uart_tx(0x0c);
    uart_tx(0x00);
#endif
}

void bt_hci_h4_command(const uint8_t *data, uint8_t len)
{
    while (!uart_tx_free());
    uart_tx(0x01);  // HCI command packet
    for (uint8_t i = 0; i < len; i++) {
        while (!uart_tx_free());
        uart_tx(data[i]);
    }
}

void bt_hci_h4_rx(uint8_t v)
{
    uint8_t rptr = bt_u4_fifo.rptr;
    uint8_t rofs = bt_u4_fifo.rofs;
    uint8_t rlen = bt_u4_fifo.rlen;
    bt_u4_fifo.event[rptr][rofs] = v;
    rofs += 1;
    bt_u4_fifo.rofs = rofs;
    if (rofs < rlen)
        return;

    switch (bt_u4_fifo.state) {
    case EventPacket:
        if (v != 0x04) {
            DBG_BKPT("Unknown type");
            bt_u4_fifo.rofs = 0;
        } else {
            bt_u4_fifo.state = EventHeader;
            bt_u4_fifo.rofs = 0;
            bt_u4_fifo.rlen = 2;
        }
        break;

    case EventHeader: {
        uint8_t plen = bt_u4_fifo.event[rptr][1];
        bt_u4_fifo.state = EventParams;
        bt_u4_fifo.rlen = 2 + plen;
        break;
    }

    case EventParams:
        bt_u4_fifo.rptr = !rptr;
        bt_u4_fifo.state = EventPacket;
        bt_u4_fifo.rofs = 0;
        bt_u4_fifo.rlen = 0;
        bt_hci_usb_event(&bt_u4_fifo.event[rptr][0], rofs);
        break;
    }
}
