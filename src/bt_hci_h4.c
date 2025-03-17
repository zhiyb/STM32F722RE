#include "bt_hci_h4.h"
#include "bt_hci_usb.h"
#include "uart.h"
#include "semihosting.h"

#define BLOCK_SIZE              64
#define TX_HCI_CMD_NUM_BLOCKS   0
#define RX_HCI_ACL_NUM_BLOCKS   8
#define RX_HCI_SYNC_NUM_BLOCKS  8
#define RX_HCI_EVENT_NUM_BLOCKS 8

typedef enum {H4Packet, H4HCIHeader, H4HCIData} event_t;

typedef struct {
    uint8_t data[BLOCK_SIZE] ALIGNED(4);
    uint8_t len;
} h4_block_t;

static struct {
    // struct {
    //     h4_block_t data[TX_HCI_CMD_NUM_BLOCKS];
    //     uint8_t wptr, rptr;
    // } cmd;
    struct {
        h4_block_t data[RX_HCI_ACL_NUM_BLOCKS];
        uint8_t wptr, rptr, cptr;   // Write, Read, Clear
    } acl;
    struct {
        h4_block_t data[RX_HCI_SYNC_NUM_BLOCKS];
        uint8_t wptr, rptr, cptr;
    } sync;
    struct {
        h4_block_t data[RX_HCI_EVENT_NUM_BLOCKS];
        uint8_t wptr, rptr, cptr;
    } event;
    uint16_t wlen, wofs;
    uint8_t wblocks, rblocks;
    uint8_t type;
    event_t state;
} bt_h4;

void bt_hci_h4_reset()
{
    // bt_h4.cmd.rptr = 0;
    // bt_h4.cmd.wptr = 0;
    bt_h4.acl.wptr = 0;
    bt_h4.acl.rptr = 0;
    bt_h4.acl.cptr = 0;
    bt_h4.sync.wptr = 0;
    bt_h4.sync.rptr = 0;
    bt_h4.sync.cptr = 0;
    bt_h4.event.wptr = 0;
    bt_h4.event.rptr = 0;
    bt_h4.event.cptr = 0;
    bt_h4.wblocks = 0;
    bt_h4.rblocks = 0;
    bt_h4.state = H4Packet;

#if 0   // HCL Reset Command
    static const uint8_t reset[3] ALIGNED(4) = {
        0x03, 0x0c, 0x00,
    };
    bt_hci_h4_command(reset, sizeof(reset));
#endif
}

void bt_hci_h4_tx(bt_h4_type_t type, const uint8_t *data, uint8_t len)
{
    while (!uart_tx_free());
    uart_tx(type);  // HCI command packet
    for (uint8_t i = 0; i < len; i++) {
        while (!uart_tx_free());
        uart_tx(data[i]);
    }
}

bool bt_hci_h4_available(bt_h4_type_t type)
{
    switch (type) {
    case BtH4HciSyncData:
        return bt_h4.sync.rptr != bt_h4.sync.wptr;
    case BtH4HciAclData:
        return bt_h4.acl.rptr != bt_h4.acl.wptr;
    case BtH4HciEvent:
        return bt_h4.event.rptr != bt_h4.event.wptr;
    }
    return false;
}

const uint8_t *bt_hci_h4_read(bt_h4_type_t type, uint8_t *len)
{
    uint8_t rptr;
    h4_block_t *block = 0;
    switch (type) {
    case BtH4HciSyncData:
        rptr = bt_h4.sync.rptr;
        if (bt_h4.sync.wptr != rptr) {
            bt_h4.sync.rptr = (rptr + 1) % RX_HCI_SYNC_NUM_BLOCKS;
            block = &bt_h4.sync.data[rptr];
        }
        break;
    case BtH4HciAclData:
        rptr = bt_h4.acl.rptr;
        if (bt_h4.acl.wptr != rptr) {
            bt_h4.acl.rptr = (rptr + 1) % RX_HCI_ACL_NUM_BLOCKS;
            block = &bt_h4.acl.data[rptr];
        }
        break;
    case BtH4HciEvent:
        rptr = bt_h4.event.rptr;
        if (bt_h4.event.wptr != rptr) {
            bt_h4.event.rptr = (rptr + 1) % RX_HCI_EVENT_NUM_BLOCKS;
            block = &bt_h4.event.data[rptr];
        }
        break;
    }
    if (!block)
        return 0;
    *len = block->len;
    return &block->data[0];
}

void bt_hci_h4_confirm(bt_h4_type_t type)
{
    switch (type) {
    case BtH4HciSyncData:
        bt_h4.sync.cptr = (bt_h4.sync.cptr + 1) % RX_HCI_SYNC_NUM_BLOCKS;
        break;
    case BtH4HciAclData:
        bt_h4.acl.cptr = (bt_h4.acl.cptr + 1) % RX_HCI_ACL_NUM_BLOCKS;
        break;
    case BtH4HciEvent:
        bt_h4.event.cptr = (bt_h4.event.cptr + 1) % RX_HCI_EVENT_NUM_BLOCKS;
        break;
    }
}

void bt_hci_h4_rx(uint8_t v)
{
    if (bt_h4.state == H4Packet) {
        // Waiting for HCI packet indicator
        bt_h4.type = v;
        switch (v) {
        case 0x02:  // HCI ACL Data Packet
            bt_h4.state = H4HCIHeader;
            bt_h4.wofs = 0;
            bt_h4.wlen = 4;
            break;
        case 0x03:  // HCI Synchronous Data Packet
            bt_h4.state = H4HCIHeader;
            bt_h4.wofs = 0;
            bt_h4.wlen = 3;
            break;
        case 0x04:  // HCI Event Packet
            bt_h4.state = H4HCIHeader;
            bt_h4.wofs = 0;
            bt_h4.wlen = 2;
            break;
        // default:
        //     DBG_BKPT("Unknown type");
        }
        return;
    }

    uint8_t wptr;
    h4_block_t *block;
    uint8_t nblocks;
    switch (bt_h4.type) {
    case 0x02:  // HCI ACL Data Packet
        wptr = bt_h4.acl.wptr;
        block = &bt_h4.acl.data[wptr];
        break;
    case 0x03:  // HCI Synchronous Data Packet
        wptr = bt_h4.sync.wptr;
        block = &bt_h4.sync.data[wptr];
        break;
    case 0x04:  // HCI Event Packet
    default:
        wptr = bt_h4.event.wptr;
        block = &bt_h4.event.data[wptr];
        break;
    }

    uint16_t wofs = bt_h4.wofs;
    block->data[wofs % BLOCK_SIZE] = v;
    wofs += 1;
    bt_h4.wofs = wofs;
    if (wofs < bt_h4.wlen && (wofs % BLOCK_SIZE) != 0)
        return;     // Incomplete

    if (bt_h4.state == H4HCIHeader) {
        // Extract data length header field
        switch (bt_h4.type) {
        case 0x02:  // HCI ACL Data Packet
            bt_h4.wlen = 4 + block->data[2] + (block->data[3] << 8);
            break;
        case 0x03:  // HCI Synchronous Data Packet
            bt_h4.wlen = 3 + block->data[2];
            break;
        case 0x04:  // HCI Event Packet
        default:
            bt_h4.wlen = 2 + block->data[1];
            break;
        }
        bt_h4.state = H4HCIData;
        if (bt_h4.wlen != bt_h4.wofs)
            return; // More data follows
    }

    // Data block complete
    block->len = (wofs % BLOCK_SIZE) ?: BLOCK_SIZE;
    switch (bt_h4.type) {
    case 0x02:  // HCI ACL Data Packet
        bt_h4.acl.wptr = (wptr + 1) % RX_HCI_ACL_NUM_BLOCKS;
        if (bt_h4.acl.wptr == bt_h4.acl.cptr)
            DBG_BKPT("overrun");
        break;
    case 0x03:  // HCI Synchronous Data Packet
        bt_h4.sync.wptr = (wptr + 1) % RX_HCI_SYNC_NUM_BLOCKS;
        if (bt_h4.sync.wptr == bt_h4.sync.cptr)
            DBG_BKPT("overrun");
        break;
    case 0x04:  // HCI Event Packet
    default:
        bt_h4.event.wptr = (wptr + 1) % RX_HCI_EVENT_NUM_BLOCKS;
        if (bt_h4.event.wptr == bt_h4.event.cptr)
            DBG_BKPT("overrun");
        break;
    }

    bt_h4.wblocks += 1;
    if (wofs < bt_h4.wlen)
        return; // More data follows

    // Packet data complete
    bt_h4.state = H4Packet;

    // Check if zero-length packet required by USB to split data segment
    if (block->len % 64 != 0)
        return;

    switch (bt_h4.type) {
    case 0x02:  // HCI ACL Data Packet
        wptr = (wptr + 1) % RX_HCI_ACL_NUM_BLOCKS;
        bt_h4.acl.data[wptr].len = 0;
        bt_h4.acl.wptr = (wptr + 1) % RX_HCI_ACL_NUM_BLOCKS;
        if (bt_h4.acl.wptr == bt_h4.acl.cptr)
            DBG_BKPT("overrun");
        break;
    // case 0x03:  // HCI Synchronous Data Packet (not possible)
    case 0x04:  // HCI Event Packet
    default:
        wptr = (wptr + 1) % RX_HCI_EVENT_NUM_BLOCKS;
        bt_h4.event.data[wptr].len = 0;
        bt_h4.event.wptr = (wptr + 1) % RX_HCI_EVENT_NUM_BLOCKS;
        if (bt_h4.event.wptr == bt_h4.event.cptr)
            DBG_BKPT("overrun");
        break;
    }
}
