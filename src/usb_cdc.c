#include "usb.h"
#include "usb_desc_hid.h"
#include "semihosting.h"

#define REQ_CLASS_SEND_ENCAPSULATED_COMMAND                     0x00
#define REQ_CLASS_GET_ENCAPSULATED_RESPONSE                     0x01
#define REQ_CLASS_SET_COMM_FEATURE                              0x02
#define REQ_CLASS_GET_COMM_FEATURE                              0x03
#define REQ_CLASS_CLEAR_COMM_FEATURE                            0x04
#define REQ_CLASS_SET_AUX_LINE_STATE                            0x10
#define REQ_CLASS_SET_HOOK_STATE                                0x11
#define REQ_CLASS_PULSE_SETUP                                   0x12
#define REQ_CLASS_SEND_PULSE                                    0x13
#define REQ_CLASS_SET_PULSE_TIME                                0x14
#define REQ_CLASS_RING_AUX_JACK                                 0x15
#define REQ_CLASS_SET_LINE_CODING                               0x20
#define REQ_CLASS_GET_LINE_CODING                               0x21
#define REQ_CLASS_SET_CONTROL_LINE_STATE                        0x22
#define REQ_CLASS_SEND_BREAK                                    0x23
#define REQ_CLASS_SET_RINGER_PARMS                              0x30
#define REQ_CLASS_GET_RINGER_PARMS                              0x31
#define REQ_CLASS_SET_OPERATION_PARMS                           0x32
#define REQ_CLASS_GET_OPERATION_PARMS                           0x33
#define REQ_CLASS_SET_LINE_PARMS                                0x34
#define REQ_CLASS_GET_LINE_PARMS                                0x35
#define REQ_CLASS_DIAL_DIGITS                                   0x36
#define REQ_CLASS_SET_UNIT_PARAMETER                            0x37
#define REQ_CLASS_GET_UNIT_PARAMETER                            0x38
#define REQ_CLASS_CLEAR_UNIT_PARAMETER                          0x39
#define REQ_CLASS_GET_PROFILE                                   0x3A
#define REQ_CLASS_SET_ETHERNET_MULTICAST_FILTERS                0x40
#define REQ_CLASS_SET_ETHERNET_POWER_MANAGEMENT_PATTERN_FILTER  0x41
#define REQ_CLASS_GET_ETHERNET_POWER_MANAGEMENT_PATTERN_FILTER  0x42
#define REQ_CLASS_SET_ETHERNET_PACKET_FILTER                    0x43
#define REQ_CLASS_GET_ETHERNET_STATISTIC                        0x44
#define REQ_CLASS_SET_ATM_DATA_FORMAT                           0x50
#define REQ_CLASS_GET_ATM_DEVICE_STATISTICS                     0x51
#define REQ_CLASS_SET_ATM_DEFAULT_VC                            0x52
#define REQ_CLASS_GET_ATM_VC_STATISTICS                         0x53
// #define REQ_CLASS_MDLM Semantic-Model specific Requests         0x60 - 0x7F
#define REQ_CLASS_GET_NTB_PARAMETERS                            0x80
#define REQ_CLASS_GET_NET_ADDRESS                               0x81
#define REQ_CLASS_SET_NET_ADDRESS                               0x82
#define REQ_CLASS_GET_NTB_FORMAT                                0x83
#define REQ_CLASS_SET_NTB_FORMAT                                0x84
#define REQ_CLASS_GET_NTB_INPUT_SIZE                            0x85
#define REQ_CLASS_SET_NTB_INPUT_SIZE                            0x86
#define REQ_CLASS_GET_MAX_DATAGRAM_SIZE                         0x87
#define REQ_CLASS_SET_MAX_DATAGRAM_SIZE                         0x88
#define REQ_CLASS_GET_CRC_MODE                                  0x89
#define REQ_CLASS_SET_CRC_MODE                                  0x8A

typedef struct PACKED {
    uint32_t dwDTERate;
    uint8_t bCharFormat;
    uint8_t bParityType;
    uint8_t bDataBits;
} line_coding_t;

static line_coding_t line_coding = {
    .dwDTERate = 115200,
    .bCharFormat = 0,       // 1 stop bit
    .bParityType = 0,       // Parity none
    .bDataBits = 8,         // Data bits
};

const void *usb_cdc_setup(setup_t *setup)
{
    switch (setup->bRequest) {
    case REQ_CLASS_SET_LINE_CODING:
        line_coding = *(const line_coding_t *)setup->data;
        return 0;

    case REQ_CLASS_GET_LINE_CODING:
        return &line_coding;

    case REQ_CLASS_SET_CONTROL_LINE_STATE: {
        const uint16_t state = setup->wValue;
        return 0;
    }

    case REQ_CLASS_SEND_BREAK: {
        uint16_t duration_ms = setup->wValue;
        return 0;
    }

    default:
        DBG_BKPT("Unknown request");
        return (void *)-1;
    }
}

enum {Tx = 0, Rx = 1};

// CDC data FIFO buffers, must be power-of-2
#define FIFO_BUF_SIZE   512

static struct {
    struct {
        volatile uint8_t buf[FIFO_BUF_SIZE];
        volatile uint16_t rdptr;
        volatile uint16_t wrptr;
    } txrx[2];
} cdc_fifo;

void usb_cdc_init()
{
    cdc_fifo.txrx[Rx].rdptr = 0;
    cdc_fifo.txrx[Rx].wrptr = 0;
    cdc_fifo.txrx[Tx].rdptr = 0;
    cdc_fifo.txrx[Tx].wrptr = 0;
}

bool usb_cdc_data_out(uint32_t *data, uint16_t len)
{
    // USB OUT i.e. RX
    // Copy to FIFO buffer
    // The USB SRAM must be addressed using 32-bit accesses
    // Also: There is no support for unaligned accesses on the Cortex-M0+ processor
    // TODO: DMA?
    uint16_t wrptr = cdc_fifo.txrx[Rx].wrptr;
    uint16_t rdptr = cdc_fifo.txrx[Rx].rdptr;
    // uint16_t avail = (FIFO_BUF_SIZE * 2 - 1 + rdptr - wrptr) % FIFO_BUF_SIZE;
    uint16_t avail = (rdptr - wrptr - 1) % FIFO_BUF_SIZE;
    if (len > avail)
        dbg_puts("FIFO overrun!\r\n");
    len = len > avail ? avail : len;
    for (uint16_t i = 0; i < (len + 3) / 4; i++) {
        uint32_t v = data[i];
        for (uint8_t ofs = 0; ofs < 4; ofs++) {
            if (i * 4 + ofs < len) {
                cdc_fifo.txrx[Rx].buf[wrptr] = v;
                wrptr = (wrptr + 1) % FIFO_BUF_SIZE;
            }
            v >>= 8;
        }
    }
    cdc_fifo.txrx[Rx].wrptr = wrptr;
    return true;
}

uint16_t usb_cdc_rx_available()
{
    uint16_t wrptr = cdc_fifo.txrx[Rx].wrptr;
    uint16_t rdptr = cdc_fifo.txrx[Rx].rdptr;
    // uint16_t avail = (FIFO_BUF_SIZE + wrptr - rdptr) % FIFO_BUF_SIZE;
    uint16_t avail = (wrptr - rdptr) % FIFO_BUF_SIZE;
    return avail;
}

uint8_t usb_cdc_rx_read()
{
    uint16_t rdptr = cdc_fifo.txrx[Rx].rdptr;
    uint8_t v = cdc_fifo.txrx[Rx].buf[rdptr];
    rdptr = (rdptr + 1) % FIFO_BUF_SIZE;
    cdc_fifo.txrx[Rx].rdptr = rdptr;
    return v;
}

void usb_cdc_data_in()
{
    // Find available TX data from rdptr to wrptr or end of buffer
    uint16_t wrptr = cdc_fifo.txrx[Tx].wrptr;
    uint16_t rdptr = cdc_fifo.txrx[Tx].rdptr;
    // uint16_t avail = (FIFO_BUF_SIZE + wrptr - rdptr) % FIFO_BUF_SIZE;
    uint16_t avail = (wrptr - rdptr) % FIFO_BUF_SIZE;
    uint16_t eob = FIFO_BUF_SIZE - rdptr;
    avail = avail < wrptr ? avail : wrptr;
    if (avail) {
        uint16_t len = 0;
        uint32_t *buf = usb_hw_ep_tx_buffer(UsbEpCDCData, &len);
        avail = avail < len ? avail : len;
        // Copy to USB SRAM
        for (uint16_t i = 0; i < (avail + 3) / 4; i++) {
            uint32_t v = 0;
            for (uint8_t ofs = 0; ofs < 4; ofs++) {
                v >>= 8;
                if (i * 4 + ofs < avail) {
                    v |= (uint32_t)cdc_fifo.txrx[Tx].buf[rdptr] << 24;
                    rdptr = (rdptr + 1) % FIFO_BUF_SIZE;
                }
            }
            buf[i] = v;
        }
        cdc_fifo.txrx[Tx].rdptr = rdptr;
        usb_hw_ep_tx(UsbEpCDCData, buf, avail, false);
    }
}

uint16_t usb_cdc_tx_free()
{
    uint16_t wrptr = cdc_fifo.txrx[Tx].wrptr;
    uint16_t rdptr = cdc_fifo.txrx[Tx].rdptr;
    // uint16_t avail = (FIFO_BUF_SIZE * 2 - 1 + rdptr - wrptr) % FIFO_BUF_SIZE;
    uint16_t avail = (rdptr - wrptr - 1) % FIFO_BUF_SIZE;
    return avail;
}

void usb_cdc_tx_write(uint8_t v)
{
    uint16_t wrptr = cdc_fifo.txrx[Tx].wrptr;
    cdc_fifo.txrx[Tx].buf[wrptr] = v;
    wrptr = (wrptr + 1) % FIFO_BUF_SIZE;
    cdc_fifo.txrx[Tx].wrptr = wrptr;

    // If endpoint TX is free, start sending data
    if (usb_hw_ep_tx_status(UsbEpCDCData) != UsbEpValid)
        usb_cdc_data_in();
}
