#include "stm32c0xx.h"
#include "semihosting.h"
#include "macros.h"
#include "usb.h"

#define TXBD(count, addr) \
    (((count) << 16) | (0x0000ffff & ((uint32_t)(addr) - USB_DRD_PMAADDR)))
#define RXBD(blsize, blocks, count, addr) \
    (((blsize) << 31) | ((blocks) << 26) | ((count) << 16) | \
        (0x0000ffff & ((uint32_t)(addr) - USB_DRD_PMAADDR)))

#define TXRXDB_PTR(bd)      ((uint32_t *)(((bd) & 0x0000ffff) + USB_DRD_PMAADDR))
#define TXRXBD_COUNT(bd)    (((bd) >> 16) & 0x03ff)

#define CHEP(ch)            (*(&USB_DRD_FS->CHEP0R + ch))
#define CHEP_MASK(val)      (((val) & 0x017f070f) | 0x7e800000)

#define CHEP_RX_DISABLED(v) ((v) & USB_CHEP_RX_STRX_Msk)
#define CHEP_RX_STALL(v)    (((v) & USB_CHEP_RX_STRX_Msk) ^ (0b01 << USB_CHEP_RX_STRX_Pos))
#define CHEP_RX_NAK(v)      (((v) & USB_CHEP_RX_STRX_Msk) ^ (0b10 << USB_CHEP_RX_STRX_Pos))
#define CHEP_RX_VALID(v)    ((~(v)) & USB_CHEP_RX_STRX_Msk)

#define CHEP_TX_DISABLED(v) ((v) & USB_CHEP_TX_STTX_Msk)
#define CHEP_TX_STALL(v)    (((v) & USB_CHEP_TX_STTX_Msk) ^ (0b01 << USB_CHEP_TX_STTX_Pos))
#define CHEP_TX_NAK(v)      (((v) & USB_CHEP_TX_STTX_Msk) ^ (0b10 << USB_CHEP_TX_STTX_Pos))
#define CHEP_TX_VALID(v)    ((~(v)) & USB_CHEP_TX_STTX_Msk)

// USB endpoint buffer allocation and management

static const uint8_t map_ch_to_ep[8] = {0, 1,};

typedef volatile struct {
    struct {
        uint32_t TXRXBD;
        uint32_t RXTXBD;
    } chep[8];
    struct {
        // For enpoint 0 control
        // Shared between TX & RX
        uint32_t buf[64 / 4];
    } ch0;
    struct {
        // For endpoint 1 HID
        uint32_t rx_buf[0 / 4];
        uint32_t tx_buf[8 / 4];
    } ch1;
    struct {
        // For endpoint 2 CDC Comm
        uint32_t rx_buf[0 / 4];
        uint32_t tx_buf[8 / 4];
    } ch2;
    struct {
        // For endpoint 3 CDC Data
        uint32_t rx_buf[64 / 4];
        uint32_t tx_buf[64 / 4];
    } ch3;
    struct {
        // For endpoint 4 BT HCI Events
        uint32_t rx_buf[0 / 4];
        uint32_t tx_buf[64 / 4];
    } ch4;
    struct {
        // For endpoint 5 BT ACL Data
        uint32_t rx_buf[64 / 4];
        uint32_t tx_buf[64 / 4];
    } ch5;
    struct {
        // For endpoint 6 BT Voice
        uint32_t rx_buf[64 / 4];
        uint32_t tx_buf[64 / 4];
    } ch6;
} usb_sram_t;

// For size checking
static usb_sram_t _sram __attribute__((section(".usbram"))) __attribute__((used));
static usb_sram_t * const usb_sram = (usb_sram_t *)USB_DRD_PMAADDR;

enum {Tx = 0, Rx = 1};

static const uint16_t ch_buf_size[8][2] = {
    // IN (TX), OUT (RX)
    {sizeof(usb_sram->ch0.buf), sizeof(usb_sram->ch0.buf)},
    {sizeof(usb_sram->ch1.tx_buf), sizeof(usb_sram->ch1.rx_buf)},
    {sizeof(usb_sram->ch2.tx_buf), sizeof(usb_sram->ch2.rx_buf)},
    {sizeof(usb_sram->ch3.tx_buf), sizeof(usb_sram->ch3.rx_buf)},
    {sizeof(usb_sram->ch4.tx_buf), sizeof(usb_sram->ch4.rx_buf)},
    {sizeof(usb_sram->ch5.tx_buf), sizeof(usb_sram->ch5.rx_buf)},
    {sizeof(usb_sram->ch6.tx_buf), sizeof(usb_sram->ch6.rx_buf)},
};

// In-progress TX/RX requests
static volatile struct {
    volatile void * data;
    uint32_t len;
} txrx_req[8];    // Channel index

static volatile struct {
    setup_t setup;
    uint8_t data[256];  // DATA OUT buffer
} ctrl_buf ALIGNED(4);

// Event queue for deferring IRQ events to main thread
#define EVENT_QUEUE_SIZE    4

typedef enum {
    EventSetup  = 0x01,
    EventOut    = 0x02,
    EventIn     = 0x04,
} event_ev_t;

typedef struct {
    uint8_t ev;
    uint8_t ep;
    uint8_t ch;
} event_t;

typedef struct {
    uint8_t event[2][8];
    uint8_t wrptr;
} hw_event_t;

static volatile hw_event_t hw_event;

static void event_push_irq(event_ev_t ev, uint8_t ch)
{
    uint8_t wrptr = hw_event.wrptr;
    hw_event.event[wrptr][ch] |= ev;
}

void usb_hw_ep_init()
{
    // Clear pending events
    hw_event = (hw_event_t){};

    // Configure channel 0 for control endpoint 0
    uint8_t ch = 0;
    usb_sram->chep[ch].TXRXBD = TXBD(0, &usb_sram->ch0.buf[0]);
    usb_sram->chep[ch].RXTXBD = RXBD(1, ch_buf_size[ch][Rx] / 32 - 1, 0, &usb_sram->ch0.buf[0]);
    txrx_req[ch].len = 0;
    ctrl_buf.setup.wLength = 0;
    // Ready for SETUP
    uint32_t chep = CHEP(ch);
    CHEP(ch) = (0b01 << USB_CHEP_UTYPE_Pos) | CHEP_RX_VALID(chep) | CHEP_TX_NAK(chep) | UsbEp0Ctrl;

    // Configure channel 1 for HID interrupt endpoint
    ch = 1;
    usb_sram->chep[ch].TXRXBD = TXBD(0, &usb_sram->ch1.tx_buf[0]);
    usb_sram->chep[ch].RXTXBD = RXBD(0, ch_buf_size[ch][Rx] / 2, 0, &usb_sram->ch1.rx_buf[0]);
    txrx_req[ch].len = 0;
    // Send NAK for now
    chep = CHEP(ch);
    CHEP(ch) = (0b11 << USB_CHEP_UTYPE_Pos) | CHEP_RX_NAK(chep) | CHEP_TX_NAK(chep) | UsbEpHid;

    // Configure channel 2 for CDC Comm interrupt endpoint
    ch = 2;
    usb_sram->chep[ch].TXRXBD = TXBD(0, &usb_sram->ch2.tx_buf[0]);
    usb_sram->chep[ch].RXTXBD = RXBD(0, ch_buf_size[ch][Rx] / 2, 0, &usb_sram->ch2.rx_buf[0]);
    txrx_req[ch].len = 0;
    // Send NAK for now
    chep = CHEP(ch);
    CHEP(ch) = (0b11 << USB_CHEP_UTYPE_Pos) | CHEP_RX_NAK(chep) | CHEP_TX_NAK(chep) | UsbEpCDCComm;

    // Configure channel 3 for CDC Data bulk endpoint
    ch = 3;
    usb_sram->chep[ch].TXRXBD = TXBD(0, &usb_sram->ch3.tx_buf[0]);
    usb_sram->chep[ch].RXTXBD = RXBD(1, ch_buf_size[ch][Rx] / 32 - 1, 0, &usb_sram->ch3.rx_buf[0]);
    txrx_req[ch].len = 0;
    // OUT ready
    chep = CHEP(ch);
    CHEP(ch) = (0b00 << USB_CHEP_UTYPE_Pos) | CHEP_RX_VALID(chep) | CHEP_TX_NAK(chep) | UsbEpCDCData;

    // Configure channel 4 for BT HCL Events interrupt IN endpoint
    ch = 4;
    usb_sram->chep[ch].TXRXBD = TXBD(0, &usb_sram->ch4.tx_buf[0]);
    usb_sram->chep[ch].RXTXBD = RXBD(1, ch_buf_size[ch][Rx] / 32 - 1, 0, &usb_sram->ch4.rx_buf[0]);
    txrx_req[ch].len = 0;
    // Send NAK for now
    chep = CHEP(ch);
    CHEP(ch) = (0b11 << USB_CHEP_UTYPE_Pos) | CHEP_RX_NAK(chep) | CHEP_TX_NAK(chep) | UsbEpBtHciEvents;

    // Configure channel 5 for BT ACL Data bulk endpoint
    ch = 5;
    usb_sram->chep[ch].TXRXBD = TXBD(0, &usb_sram->ch5.tx_buf[0]);
    usb_sram->chep[ch].RXTXBD = RXBD(1, ch_buf_size[ch][Rx] / 32 - 1, 0, &usb_sram->ch5.rx_buf[0]);
    txrx_req[ch].len = 0;
    // Send NAK for now
    chep = CHEP(ch);
    CHEP(ch) = (0b00 << USB_CHEP_UTYPE_Pos) | CHEP_RX_NAK(chep) | CHEP_TX_NAK(chep) | UsbEpBtACLData;

    // Configure channel 6 for BT Voice isochronous endpoint
    ch = 6;
    usb_sram->chep[ch].TXRXBD = TXBD(0, &usb_sram->ch6.tx_buf[0]);
    usb_sram->chep[ch].RXTXBD = RXBD(1, ch_buf_size[ch][Rx] / 32 - 1, 0, &usb_sram->ch6.rx_buf[0]);
    txrx_req[ch].len = 0;
    // Send NAK for now
    chep = CHEP(ch);
    CHEP(ch) = (0b00 << USB_CHEP_UTYPE_Pos) | CHEP_RX_NAK(chep) | CHEP_TX_NAK(chep) | UsbEpBtVoice;
}

static inline uint8_t ep_to_ch(uint8_t ep)
{
    return ep;
}

void usb_hw_ep_tx(uint8_t ep, const void *data, uint32_t len, bool status_out)
{
    uint8_t ch = ep_to_ch(ep);
    const uint16_t max_len = ch_buf_size[ch][Tx];

    if (len > max_len) {
        txrx_req[ch].data = (void *)data + max_len;
        txrx_req[ch].len = len - max_len;
        len = max_len;
    } else {
        txrx_req[ch].len = 0;
    }

    // Configure SRAM buffer channel
    uint32_t *dst = TXRXDB_PTR(usb_sram->chep[ch].TXRXBD);  // TODO Double buffering support?
    const uint32_t *src = data;
    for (uint32_t i = 0; i < (len + 3) / 4; i++)
        dst[i] = src[i];
    usb_sram->chep[ch].TXRXBD = TXBD(len, dst);

    // Enable endpoint channel for IN transfer
    uint32_t chep = CHEP(ch);
    CHEP(ch) = CHEP_MASK(chep) | (status_out ? USB_CHEP_KIND_Msk : 0) | CHEP_TX_VALID(chep);
}

void usb_hw_ep_tx_stall(uint8_t ep)
{
    uint8_t ch = ep_to_ch(ep);
    uint32_t chep = CHEP(ch);
    CHEP(ch) = CHEP_MASK(chep) | CHEP_TX_STALL(chep);
}

void usb_hw_ep_tx_nak(uint8_t ep)
{
    uint8_t ch = ep_to_ch(ep);
    uint32_t chep = CHEP(ch);
    CHEP(ch) = CHEP_MASK(chep) | CHEP_TX_NAK(chep);
}

uint32_t *usb_hw_ep_tx_buffer(uint8_t ep, uint16_t *len)
{
    uint8_t ch = ep_to_ch(ep);
    if (len)
        *len = ch_buf_size[ch][Tx];
    return TXRXDB_PTR(usb_sram->chep[ch].TXRXBD);
}

usb_endpoint_status_t usb_hw_ep_tx_status(uint8_t ep)
{
    uint32_t chep = CHEP(ep_to_ch(ep));
    return (chep >> USB_CHEP_TX_STTX_Pos) & 0b11;
}

usb_endpoint_status_t usb_hw_ep_rx_status(uint8_t ep)
{
    uint32_t chep = CHEP(ep_to_ch(ep));
    return (chep >> USB_CHEP_RX_STRX_Pos) & 0b11;
}


static volatile uint16_t usb_daddr = 0;

void usb_hw_set_address(uint16_t addr)
{
    usb_daddr = addr;
}

void usb_hw_ep_ctr_irq()
{
    for (uint8_t ch = 0; ch < 8; ch++) {
        uint32_t chep = CHEP(ch);
        uint8_t ep = chep & 0x0f;
        if (ch != 0 && ep == 0)
            break;

        if (chep & USB_CHEP_VTRX_Msk) {
            // RX complete, SETUP or OUT
            event_ev_t ev = EventOut;
            switch (chep & USB_CHEP_UTYPE_Msk) {
            case 0b01 << USB_CHEP_UTYPE_Pos:    // Control
                ev = EventSetup;
                if (chep & USB_CHEP_SETUP_Msk) {
                    // SETUP
                    uint32_t bd = usb_sram->chep[ch].RXTXBD;
                    uint32_t len = TXRXBD_COUNT(bd);
                    if (len != 8) {
                        // Invalid setup packet size, ignore
                        CHEP(ch) = CHEP_MASK(chep) | CHEP_RX_VALID(chep);
                    } else {
                        // Data from USBRAM can only be accessed by word
                        // Copy to local buffer for easy access
                        uint32_t *dst = (uint32_t *)&ctrl_buf.setup;
                        uint32_t *src = TXRXDB_PTR(bd);
                        dst[0] = src[0];
                        dst[1] = src[1];

                        setup_t *setup = (setup_t *)&ctrl_buf.setup;
                        if (((setup->bmRequestType & 0x80) == 0) && (setup->wLength != 0)) {
                            // Continue receiving for DATA OUT
                            txrx_req[ch].data = (void volatile *)&ctrl_buf.data[0];
                            if (setup->wLength > sizeof(ctrl_buf.data)) {
                                DBG_BKPT("Insufficient buffer size");
                                txrx_req[ch].len = 0;
                            } else {
                                txrx_req[ch].len = setup->wLength;
                            }
                            CHEP(ch) = (CHEP_MASK(chep) | CHEP_RX_VALID(chep)) & ~USB_CHEP_KIND_Msk;

                        } else {
                            // SETUP complete, defer to main thread
                            event_push_irq(EventSetup, ch);
                            CHEP(ch) = CHEP_MASK(chep);
                        }
                    }
                    break;

                } else if (TXRXBD_COUNT(usb_sram->chep[ch].RXTXBD) == 0) {
                    // 0-length data, STATUS OUT
                    // Wait for SETUP
                    CHEP(ch) = CHEP_MASK(chep) | CHEP_RX_VALID(chep);
                    break;
                }

                // DATA OUT, fall-through

            case 0b11 << USB_CHEP_UTYPE_Pos: {  // Interrupt (or control)
                // DATA OUT
                uint32_t bd = usb_sram->chep[ch].RXTXBD;
                uint16_t len = TXRXBD_COUNT(bd);
                uint32_t buf_len = txrx_req[ch].len;
                len = len < buf_len ? len : buf_len;
                // Copy to data buffer
                uint32_t *dst = (uint32_t *)txrx_req[ch].data;
                uint32_t *src = TXRXDB_PTR(bd);
                for (uint16_t i = 0; i < (len + 3) / 4; i++)
                    dst[i] = src[i];
                buf_len -= len;
                txrx_req[ch].data = dst + len / 4;
                txrx_req[ch].len = buf_len;

                if (buf_len == 0) {
                    // DATA OUT complete, defer to main thread
                    event_push_irq(ev, ch);
                    // Keep NAK, wait for main thread
                    CHEP(ch) = CHEP_MASK(chep);
                } else {
                    // More data to be received
                    CHEP(ch) = CHEP_MASK(chep) | CHEP_RX_VALID(chep);
                }
                break;
            }

            case 0b00 << USB_CHEP_UTYPE_Pos:    // Bulk
                // Defer to main thread
                event_push_irq(EventOut, ch);
                CHEP(ch) = CHEP_MASK(chep);
                break;

            default:
                DBG_BKPT("Unhandled endpoint type");
            }

        } else if (chep & USB_CHEP_VTTX_Msk) {
            // DATA IN complete
            switch (chep & USB_CHEP_UTYPE_Msk) {
            case 0b01 << USB_CHEP_UTYPE_Pos:    // Control
                if (txrx_req[ch].len) {
                    // Continue with remaining data
                    usb_hw_ep_tx(ep, (void *)txrx_req[ch].data, txrx_req[ch].len, true);
                } else {
                    // Wait for SETUP / STATUS OUT
                    CHEP(ch) = CHEP_MASK(chep) | CHEP_RX_VALID(chep);
                    if (!(chep & USB_CHEP_KIND_Msk)) {
                        // STATUS IN, update device address
                        USB_DRD_FS->DADDR = USB_DADDR_EF_Msk | usb_daddr;
                    }
                }
                break;

            case 0b11 << USB_CHEP_UTYPE_Pos:    // Interrupt
                if (txrx_req[ch].len) {
                    // Continue with remaining data
                    usb_hw_ep_tx(ep, (void *)txrx_req[ch].data, txrx_req[ch].len, true);
                } else {
                    // No more data, keep NAK
                    CHEP(ch) = CHEP_MASK(chep);
                }
                break;

            case 0b00 << USB_CHEP_UTYPE_Pos:    // Bulk
                // Defer to main thread
                event_push_irq(EventIn, ch);
                CHEP(ch) = CHEP_MASK(chep);
                break;

            default:
                DBG_BKPT("Unhandled endpoint type");
            }

        } else if (chep & 0x7e808080) {
            DBG_BKPT("Unhandled event");
        }
    }
}

void usb_hw_ep_process()
{
    // Swap event bitmap buffer
    uint8_t rdptr = hw_event.wrptr;
    hw_event.wrptr = !rdptr;
    for (uint8_t ch = 0; ch < 8; ch++) {
        uint8_t ev = hw_event.event[rdptr][ch];
        if (ev) {
            hw_event.event[rdptr][ch] = 0;
            uint32_t chep = CHEP(ch);
            uint8_t ep = chep & 0x0f;

            if (ev & EventSetup) {
                usb_ep0_setup(&ctrl_buf.setup);
            }

            if (ev & EventOut) {
                uint32_t bd = usb_sram->chep[ch].RXTXBD;
                uint32_t *data = TXRXDB_PTR(bd);
                uint16_t len = TXRXBD_COUNT(bd);
                bool rx_valid = false;
                if (ep == UsbEpCDCData) {
                    rx_valid = usb_cdc_data_out(data, len);
                } else {
                    DBG_BKPT("Unknown endpoint");
                }
                CHEP(ch) = CHEP_MASK(chep) | (rx_valid ? CHEP_RX_VALID(chep) : 0);
            }

            if (ev & EventIn) {
                if (ep == UsbEpCDCData) {
                    usb_cdc_data_in();
                } else {
                    DBG_BKPT("Unknown endpoint");
                }
            }
        }
    }
}
