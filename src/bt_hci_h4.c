#include "bt_hci_h4.h"
#include "bt_hci_usb.h"
#include "uart.h"
#include "semihosting.h"

#define RX_BLOCK_SIZE   64
#define RX_NUM_BLOCKS   8

static struct {
    struct {
        uint8_t data[RX_BLOCK_SIZE] ALIGNED(4);
        uint8_t type, len;
    } block[RX_NUM_BLOCKS];
    uint16_t wlen;
    uint8_t wptr, wofs;
    uint8_t rptr;
    enum {H4Packet, H4HCIHeader, H4HCIData} state;
} bt_h4_rx;

void bt_hci_h4_reset()
{
    bt_h4_rx.rptr = 0;
    bt_h4_rx.wptr = 0;
    bt_h4_rx.state = H4Packet;
#if 0   // HCL Reset Command
    static const uint8_t reset[3] ALIGNED(4) = {
        0x03, 0x0c, 0x00,
    };
    bt_hci_h4_command(reset, sizeof(reset));
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
    const uint8_t wptr = bt_h4_rx.wptr;
    const uint8_t wlen = bt_h4_rx.wlen;
    uint8_t wofs = bt_h4_rx.wofs;
    bt_h4_rx.block[wptr].data[wofs % RX_BLOCK_SIZE] = v;
    wofs += 1;
    bt_h4_rx.wofs = wofs;
    if (wofs < wlen && (wofs % RX_BLOCK_SIZE) != 0)
        return;

    switch (bt_h4_rx.state) {
    case H4Packet:
        bt_h4_rx.block[wptr].type = v;
        switch (v) {
        case 0x02:  // HCI ACL Data Packet
            bt_h4_rx.state = H4HCIHeader;
            bt_h4_rx.wofs = 0;
            bt_h4_rx.wlen = 4;
            break;
        case 0x04:  // HCI Event Packet
            bt_h4_rx.state = H4HCIHeader;
            bt_h4_rx.wofs = 0;
            bt_h4_rx.wlen = 2;
            break;
        default:
            DBG_BKPT("Unknown type");
            bt_h4_rx.wofs = 0;
        }
        break;

    case H4HCIHeader:
        switch (bt_h4_rx.block[wptr].type) {
        case 0x02:  // HCI ACL Data Packet
            bt_h4_rx.state = H4HCIData;
            bt_h4_rx.wlen = 4 + bt_h4_rx.block[wptr].data[2] +
                (bt_h4_rx.block[wptr].data[3] << 8);
            break;
        case 0x04:  // HCI Event Packet
            bt_h4_rx.state = H4HCIData;
            bt_h4_rx.wlen = 2 + bt_h4_rx.block[wptr].data[1];
            break;
        default:
            TODO();
            break;
        }
        break;

    case H4HCIData: {
        // Block complete
        uint8_t type = bt_h4_rx.block[wptr].type;
        uint8_t len = (wofs % RX_BLOCK_SIZE) ?: RX_BLOCK_SIZE;
        uint8_t wptr_n = (wptr + 1) % RX_NUM_BLOCKS;
        bt_h4_rx.block[wptr].len = len;
        bt_h4_rx.wptr = wptr_n;
        if (wofs < wlen) {
            // More data follows
            bt_h4_rx.block[wptr_n].type = type;
        } else {
            // Packet complete
            bt_h4_rx.state = H4Packet;
            bt_h4_rx.wofs = 0;
            bt_h4_rx.wlen = 0;
        }
        // Send completed block to USB
        switch (type) {
        case 0x02:  // HCI ACL Data Packet
            TODO();
            break;
        case 0x04:  // HCI Event Packet
            bt_hci_usb_event(&bt_h4_rx.block[wptr].data[0], len);
            break;
        default:
            TODO();
            break;
        }
        break;
    }

    }
}
