#include "stm32c0xx.h"
#include "semihosting.h"
#include "macros.h"
#include "bt_hci_usb.h"
#include "usb.h"

#define TXBD(count, addr) \
    (((count) << 16) | (0x0000ffff & ((uint32_t)(addr) - USB_DRD_PMAADDR)))
#define RXBD(blsize, blocks, count, addr) \
    (((blsize) << 31) | ((blocks) << 26) | ((count) << 16) | \
        (0x0000ffff & ((uint32_t)(addr) - USB_DRD_PMAADDR)))

#define TXRXDB_PTR(bd)      ((uint32_t *)(((bd) & 0x0000ffff) + USB_DRD_PMAADDR))
#define TXRXBD_COUNT(bd)    (((bd) >> 16) & 0x03ff)

#define CHEP(ch)            (*(&USB_DRD_FS->CHEP0R + ch))
#define CHEP_MASK(val)      (((val) & 0x017f070f) | 0x7e808080)

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
        // For IN endpoint 1 BT HCI Events
        uint32_t rx_buf[0 / 4];
        uint32_t tx_buf[64 / 4];
    } ch1;
    struct {
        // For OUT endpoint 2 BT ACL Data
        uint32_t rx_buf[2][64 / 4];
    } ch2;
    struct {
        // For OUT endpoint 3 BT Voice
        uint32_t rx_buf[2][64 / 4];
    } ch3;
    struct {
        // For endpoint 4 HID
        // For endpoint 4 CDC Comm
        uint32_t rx_buf[64 / 4];
        uint32_t tx_buf[64 / 4];
    } ch4;
    struct {
        // For endpoint 5 CDC Data
        uint32_t rx_buf[64 / 4];
        uint32_t tx_buf[64 / 4];
    } ch5;
    struct {
        // For IN endpoint 2 BT ACL Data
        uint32_t tx_buf[2][64 / 4];
    } ch6;
    struct {
        // For IN endpoint 3 BT Voice
        uint32_t tx_buf[2][64 / 4];
    } ch7;
} usb_sram_t;

// For size checking
static usb_sram_t _usb_sram __attribute__((section(".usbram"))) __attribute__((used));
static usb_sram_t * const usb_sram = (usb_sram_t *)USB_DRD_PMAADDR;

enum {Tx = 0, Rx = 1};

static const uint16_t ch_buf_size[8][2] = {
    // IN (TX), OUT (RX)
    {sizeof(usb_sram->ch0.buf), sizeof(usb_sram->ch0.buf)},
    {sizeof(usb_sram->ch1.tx_buf), sizeof(usb_sram->ch1.rx_buf)},
    {0, sizeof(usb_sram->ch2.rx_buf[0])},
    {0, sizeof(usb_sram->ch3.rx_buf[0])},
    {sizeof(usb_sram->ch4.tx_buf), sizeof(usb_sram->ch4.rx_buf)},
    {sizeof(usb_sram->ch5.tx_buf), sizeof(usb_sram->ch5.rx_buf)},
    {sizeof(usb_sram->ch6.tx_buf[0]), 0},
    {sizeof(usb_sram->ch7.tx_buf[0]), 0},
};

// In-progress TX/RX requests
static struct {
    volatile void * volatile data;
    volatile uint32_t len;
    volatile uint8_t db_valid[2];
    volatile uint8_t first;
} txrx_req[8];          // Channel index

static volatile struct {
    setup_t setup;
    uint8_t data[256];  // DATA OUT buffer
} ctrl_buf ALIGNED(4);

// Event queue for deferring IRQ events to main thread
#define EVENT_QUEUE_SIZE    4

typedef enum {
    EventSetup  = 0x01,
    EventOut    = 0x02,
    EventIn     = 0x10,
} event_ev_t;

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
    uint8_t ch = UsbEp0Ctrl;
    usb_sram->chep[ch].TXRXBD = TXBD(0, &usb_sram->ch0.buf[0]);
    usb_sram->chep[ch].RXTXBD = RXBD(1, ch_buf_size[ch][Rx] / 32 - 1, 0, &usb_sram->ch0.buf[0]);
    txrx_req[ch].len = 0;
    ctrl_buf.setup.wLength = 0;
    // Ready for SETUP
    uint32_t chep = CHEP(ch);
    CHEP(ch) = (0b01 << USB_CHEP_UTYPE_Pos) | CHEP_RX_VALID(chep) | CHEP_TX_NAK(chep) | ch;

    // Configure channel 1 for BT HCL Events interrupt IN endpoint
    ch = UsbEpBtHciEvents;
    usb_sram->chep[ch].TXRXBD = TXBD(0, &usb_sram->ch1.tx_buf[0]);
    usb_sram->chep[ch].RXTXBD = RXBD(1, ch_buf_size[ch][Rx] / 32 - 1, 0, &usb_sram->ch1.rx_buf[0]);
    txrx_req[ch].len = 0;
    // Send NAK for now
    chep = CHEP(ch);
    CHEP(ch) = (0b11 << USB_CHEP_UTYPE_Pos) | CHEP_RX_DISABLED(chep) | CHEP_TX_NAK(chep) | ch;

    // Configure channel 2 for BT ACL Data bulk OUT endpoint, with double buffering
    ch = UsbEpBtACLData;
    usb_sram->chep[ch].TXRXBD = RXBD(1, ch_buf_size[ch][Rx] / 32 - 1, 0, &usb_sram->ch2.rx_buf[1][0]);
    usb_sram->chep[ch].RXTXBD = RXBD(1, ch_buf_size[ch][Rx] / 32 - 1, 0, &usb_sram->ch2.rx_buf[0][0]);
    txrx_req[ch].len = 0;
    txrx_req[ch].db_valid[0] = 0;
    txrx_req[ch].db_valid[1] = 0;
    chep = CHEP(ch);
#if 0
    // Send NAK for now
    CHEP(ch) = (0b00 << USB_CHEP_UTYPE_Pos) | USB_CHEP_KIND_Msk | CHEP_RX_NAK(chep) | CHEP_TX_DISABLED(chep) |
        (chep & (USB_CHEP_DTOG_TX_Msk | USB_CHEP_DTOG_RX_Msk)) | ch;
#elif 1
    // Ready for OUT
    CHEP(ch) = (0b00 << USB_CHEP_UTYPE_Pos) | USB_CHEP_KIND_Msk | CHEP_RX_VALID(chep) | CHEP_TX_DISABLED(chep) |
        ((chep ^ USB_CHEP_DTOG_TX_Msk) & (USB_CHEP_DTOG_TX_Msk | USB_CHEP_DTOG_RX_Msk)) | ch;
#else
    // Ready for OUT
    CHEP(ch) = (0b00 << USB_CHEP_UTYPE_Pos) | USB_CHEP_KIND_Msk | CHEP_RX_VALID(chep) | CHEP_TX_DISABLED(chep) |
        ((chep ^ USB_CHEP_DTOG_TX_Msk) & (USB_CHEP_DTOG_TX_Msk | USB_CHEP_DTOG_RX_Msk)) | ch;
#endif

    // Configure channel 6 for BT ACL Data bulk IN endpoint, with double buffering
    ch = UsbEpBtACLDataIn;
    usb_sram->chep[ch].TXRXBD = RXBD(1, ch_buf_size[ch][Rx] / 32 - 1, 0, &usb_sram->ch6.tx_buf[0][0]);
    usb_sram->chep[ch].RXTXBD = RXBD(1, ch_buf_size[ch][Rx] / 32 - 1, 0, &usb_sram->ch6.tx_buf[1][0]);
    txrx_req[ch].len = 0;
    txrx_req[ch].db_valid[0] = 0;
    txrx_req[ch].db_valid[1] = 0;
    txrx_req[ch].first = 1;
    // Send NAK for now
    chep = CHEP(ch);
    CHEP(ch) = (0b00 << USB_CHEP_UTYPE_Pos) | USB_CHEP_KIND_Msk | CHEP_RX_DISABLED(chep) | CHEP_TX_NAK(chep) |
        ((chep ^ USB_CHEP_DTOG_TX_Msk) & (USB_CHEP_DTOG_TX_Msk | USB_CHEP_DTOG_RX_Msk)) | UsbEpBtACLData;

    // Configure channel 3 for BT Voice isochronous OUT endpoint
    ch = UsbEpBtVoice;
    usb_sram->chep[ch].TXRXBD = RXBD(1, ch_buf_size[ch][Rx] / 32 - 1, 0, &usb_sram->ch3.rx_buf[1][0]);
    usb_sram->chep[ch].RXTXBD = RXBD(1, ch_buf_size[ch][Rx] / 32 - 1, 0, &usb_sram->ch3.rx_buf[0][0]);
    txrx_req[ch].len = 0;
    txrx_req[ch].db_valid[0] = 0;
    txrx_req[ch].db_valid[1] = 0;
    // Disabled for now
    chep = CHEP(ch);
    CHEP(ch) = (0b00 << USB_CHEP_UTYPE_Pos) | CHEP_RX_DISABLED(chep) | CHEP_TX_DISABLED(chep) |
        (chep & (USB_CHEP_DTOG_TX_Msk | USB_CHEP_DTOG_RX_Msk)) | ch;

    // Configure channel 7 for BT Voice isochronous IN endpoint
    ch = UsbEpBtVoiceIn;
    usb_sram->chep[ch].TXRXBD = RXBD(1, ch_buf_size[ch][Rx] / 32 - 1, 0, &usb_sram->ch7.tx_buf[0][0]);
    usb_sram->chep[ch].RXTXBD = RXBD(1, ch_buf_size[ch][Rx] / 32 - 1, 0, &usb_sram->ch7.tx_buf[1][0]);
    txrx_req[ch].len = 0;
    txrx_req[ch].db_valid[0] = 0;
    txrx_req[ch].db_valid[1] = 0;
    // Disabled for now
    chep = CHEP(ch);
    CHEP(ch) = (0b00 << USB_CHEP_UTYPE_Pos) | CHEP_RX_DISABLED(chep) | CHEP_TX_DISABLED(chep) |
        (chep & (USB_CHEP_DTOG_TX_Msk | USB_CHEP_DTOG_RX_Msk)) | UsbEpBtVoice;

    // Endpoints with alternative settings

#if USB_ALT_IF == USB_ALT_IF_HID
    // Configure channel 4 for HID interrupt endpoint
    ch = UsbEpHid;
    usb_sram->chep[ch].TXRXBD = TXBD(0, &usb_sram->ch4.tx_buf[0]);
    usb_sram->chep[ch].RXTXBD = RXBD(1, ch_buf_size[ch][Rx] / 32 - 1, 0, &usb_sram->ch4.rx_buf[0]);
    txrx_req[ch].len = 0;
    // Send NAK for now
    chep = CHEP(ch);
    CHEP(ch) = (0b11 << USB_CHEP_UTYPE_Pos) | CHEP_RX_NAK(chep) | CHEP_TX_NAK(chep) | ch;

#elif USB_ALT_IF == USB_ALT_IF_CDC
    // Configure channel 4 for CDC Comm interrupt endpoint
    ch = UsbEpCDCComm;
    usb_sram->chep[ch].TXRXBD = TXBD(0, &usb_sram->ch4.tx_buf[0]);
    usb_sram->chep[ch].RXTXBD = RXBD(1, ch_buf_size[ch][Rx] / 32 - 1, 0, &usb_sram->ch4.rx_buf[0]);
    txrx_req[ch].len = 0;
    // Send NAK for now
    chep = CHEP(ch);
    CHEP(ch) = (0b11 << USB_CHEP_UTYPE_Pos) | CHEP_RX_DISABLED(chep) | CHEP_TX_NAK(chep) | ch;

    // Configure channel 5 for CDC Data bulk endpoint
    ch = UsbEpCDCData;
    usb_sram->chep[ch].TXRXBD = TXBD(0, &usb_sram->ch5.tx_buf[0]);
    usb_sram->chep[ch].RXTXBD = RXBD(1, ch_buf_size[ch][Rx] / 32 - 1, 0, &usb_sram->ch5.rx_buf[0]);
    txrx_req[ch].len = 0;
    // OUT ready
    chep = CHEP(ch);
    CHEP(ch) = (0b00 << USB_CHEP_UTYPE_Pos) | CHEP_RX_VALID(chep) | CHEP_TX_NAK(chep) | ch;
#endif

#if 0
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
#endif
}

static inline uint8_t ep_to_tx_ch(uint8_t ep)
{
#if 0
    if (ep == UsbEpBtACLData)
        return UsbEpBtACLDataIn;
    else if (ep == UsbEpBtVoice)
        return UsbEpBtVoiceIn;
#endif
    return ep;
}

static inline uint8_t ep_to_rx_ch(uint8_t ep)
{
    return ep;
}

void usb_hw_ep_tx(uint8_t ep, const void *data, uint32_t len, bool status_out)
{
    uint8_t ch = ep_to_tx_ch(ep);
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

bool usb_hw_ep_tx_db_available(uint8_t ep)
{
    // TX with double buffering
    uint8_t ch = ep_to_tx_ch(ep);
    uint32_t chep = CHEP(ch);
    uint8_t sw_buf = !!(chep & USB_CHEP_DTOG_RX_Msk);
    return !txrx_req[ch].db_valid[sw_buf];
}

void usb_hw_ep_tx_db(uint8_t ep, const void *data, uint32_t len)
{
    // TX with double buffering
    uint8_t ch = ep_to_tx_ch(ep);
    const uint16_t max_len = ch_buf_size[ch][Tx];

    if (len > max_len) {
        txrx_req[ch].data = (void *)data + max_len;
        txrx_req[ch].len = len - max_len;
        len = max_len;
    } else {
        txrx_req[ch].len = 0;
    }

    // Find next SW buffer
    uint32_t chep = CHEP(ch);
    bool sw_buf = !!(chep & USB_CHEP_DTOG_RX_Msk);
    volatile uint32_t *bd = sw_buf ? &usb_sram->chep[ch].RXTXBD : &usb_sram->chep[ch].TXRXBD;

    // Copy to USB SRAM buffer
    uint32_t *dst = TXRXDB_PTR(*bd);
    const uint32_t *src = data;
    for (uint32_t i = 0; i < (len + 3) / 4; i++)
        dst[i] = src[i];
    *bd = TXBD(len, dst);
    txrx_req[ch].db_valid[sw_buf] = 1;

    chep = CHEP(ch);
    if (txrx_req[ch].first ? sw_buf == 0 :
        !!(chep & USB_CHEP_DTOG_TX_Msk) == sw_buf) {
        // Endpoint is currently stalled, start transfer
        CHEP(ch) = CHEP_MASK(chep) | CHEP_TX_VALID(chep) | USB_CHEP_DTOG_RX_Msk;
    }
}

void usb_hw_ep_tx_stall(uint8_t ep)
{
    uint8_t ch = ep_to_tx_ch(ep);
    uint32_t chep = CHEP(ch);
    CHEP(ch) = CHEP_MASK(chep) | CHEP_TX_STALL(chep);
}

void usb_hw_ep_tx_nak(uint8_t ep)
{
    uint8_t ch = ep_to_tx_ch(ep);
    uint32_t chep = CHEP(ch);
    CHEP(ch) = CHEP_MASK(chep) | CHEP_TX_NAK(chep);
}

uint32_t *usb_hw_ep_tx_buffer(uint8_t ep, uint16_t *len)
{
    uint8_t ch = ep_to_tx_ch(ep);
    if (len)
        *len = ch_buf_size[ch][Tx];
    return TXRXDB_PTR(usb_sram->chep[ch].TXRXBD);
}

usb_endpoint_status_t usb_hw_ep_tx_status(uint8_t ep)
{
    uint32_t chep = CHEP(ep_to_tx_ch(ep));
    return (chep >> USB_CHEP_TX_STTX_Pos) & 0b11;
}

usb_endpoint_status_t usb_hw_ep_rx_status(uint8_t ep)
{
    uint32_t chep = CHEP(ep_to_rx_ch(ep));
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
                        CHEP(ch) = (CHEP_MASK(chep) & ~USB_CHEP_VTRX_Msk) | CHEP_RX_VALID(chep);
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
                            CHEP(ch) = (CHEP_MASK(chep) & ~(USB_CHEP_KIND_Msk | USB_CHEP_VTRX_Msk)) | CHEP_RX_VALID(chep);

                        } else {
                            // SETUP complete, defer to main thread
                            event_push_irq(EventSetup, ch);
                            CHEP(ch) = CHEP_MASK(chep) & ~USB_CHEP_VTRX_Msk;
                        }
                    }
                    break;

                } else if (TXRXBD_COUNT(usb_sram->chep[ch].RXTXBD) == 0) {
                    // 0-length data, STATUS OUT
                    // Wait for SETUP
                    CHEP(ch) = (CHEP_MASK(chep) & ~USB_CHEP_VTRX_Msk) | CHEP_RX_VALID(chep);
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
                    CHEP(ch) = CHEP_MASK(chep) & ~USB_CHEP_VTRX_Msk;
                } else {
                    // More data to be received
                    CHEP(ch) = (CHEP_MASK(chep) & ~USB_CHEP_VTRX_Msk) | CHEP_RX_VALID(chep);
                }
                break;
            }

            case 0b00 << USB_CHEP_UTYPE_Pos:    // Bulk
                if (chep & USB_CHEP_KIND_Msk) {
#if 0
                    // Double buffering support
                    bool dtog = !!(chep & USB_CHEP_DTOG_RX_Msk);
                    bool sw_buf = !!(chep & USB_CHEP_DTOG_TX_Msk);
                    txrx_req[ch].db_valid[!dtog] = 1;
                    // dbg_bkpt();
                    if (!txrx_req[ch].db_valid[sw_buf]) {
                        // Other buffer is available for OUT
                        CHEP(ch) = CHEP_MASK(chep) | CHEP_RX_VALID(chep) | USB_CHEP_DTOG_TX_Msk;
                    }
#endif
                }
                // Send OUT event to main thread
                event_push_irq(EventOut, ch);
                CHEP(ch) = CHEP_MASK(chep) & ~USB_CHEP_VTRX_Msk;
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
                    CHEP(ch) = CHEP_MASK(chep) & ~USB_CHEP_VTTX_Msk;
                } else {
                    // Wait for SETUP / STATUS OUT
                    CHEP(ch) = (CHEP_MASK(chep) & ~USB_CHEP_VTTX_Msk) | CHEP_RX_VALID(chep);
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
                    // No more data
                    event_push_irq(EventIn, ch);
                }
                CHEP(ch) = CHEP_MASK(chep) & ~USB_CHEP_VTTX_Msk;
                break;

            case 0b00 << USB_CHEP_UTYPE_Pos:    // Bulk
                if (chep & USB_CHEP_KIND_Msk) {
                    // Double buffering support
                    txrx_req[ch].first = 0;
                    bool sw_buf = !!(chep & USB_CHEP_DTOG_RX_Msk);
                    txrx_req[ch].db_valid[!sw_buf] = 0;
                    if (txrx_req[ch].db_valid[sw_buf]) {
                        // Data buffer has been filled already, continue
                        CHEP(ch) = CHEP_MASK(chep) | CHEP_TX_VALID(chep) | USB_CHEP_DTOG_RX_Msk;
                    }
                }
                // Send IN complete event to main thread
                event_push_irq(EventIn, ch);
                CHEP(ch) = CHEP_MASK(chep) & ~USB_CHEP_VTTX_Msk;
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
                usb_ep0_setup((setup_t *)&ctrl_buf.setup);
            }

            if (ev & EventOut) {
                if ((chep & USB_CHEP_UTYPE_Msk) == (0b10 << USB_CHEP_UTYPE_Pos) ||
                    (chep & (USB_CHEP_UTYPE_Msk | USB_CHEP_KIND_Msk)) == ((0b00 << USB_CHEP_UTYPE_Pos) | USB_CHEP_KIND_Msk)) {
                    // Double buffering enabled
                    bool sw_buf = !!(chep & USB_CHEP_DTOG_TX_Msk);
                    uint32_t bd = sw_buf ? usb_sram->chep[ch].TXRXBD : usb_sram->chep[ch].RXTXBD;
                    uint32_t *data = TXRXDB_PTR(bd);
                    uint16_t len = TXRXBD_COUNT(bd);
                    bool rx_valid = false;
                    switch (ep) {
                    case UsbEpBtACLData:
                        rx_valid = bt_hci_usb_acl_tx(data, len);
                        break;
                    default:
                        DBG_BKPT("Unknown endpoint");
                    }
                    if (rx_valid) {
                        // Ready for next RX
                        txrx_req[ch].db_valid[sw_buf] = 0;
                        uint32_t chep = CHEP(ch);
                        if (!!(chep & USB_CHEP_DTOG_RX_Msk) == !!(chep & USB_CHEP_DTOG_TX_Msk)) {
                            // RX stalled, toggle SW_BUF to get ready
                            CHEP(ch) = CHEP_MASK(chep) | CHEP_RX_VALID(chep) | USB_CHEP_DTOG_TX_Msk;
                        }
                    }
                } else {
                    uint32_t bd = usb_sram->chep[ch].RXTXBD;
                    uint32_t *data = TXRXDB_PTR(bd);
                    uint16_t len = TXRXBD_COUNT(bd);
                    bool rx_valid = false;
                    switch (ep) {
                    case UsbEpCDCData:
                        rx_valid = usb_cdc_data_out(data, len);
                        break;
                    default:
                        DBG_BKPT("Unknown endpoint");
                    }
                    if (rx_valid) {
                        // Ready for next RX
                        CHEP(ch) = CHEP_MASK(chep) | CHEP_RX_VALID(chep);
                    }
                }
            }

            if (ev & EventIn) {
                switch (ep) {
                case UsbEp0Ctrl:
                    break;
                case UsbEpBtHciEvents:
                    bt_hci_usb_event_confirm();
                    break;
                case UsbEpBtACLData:
                    bt_hci_usb_acl_confirm();
                    break;
                case UsbEpCDCData:
                    usb_cdc_data_in();
                    break;
                default:
                    DBG_BKPT("Unknown endpoint");
                }
            }
        }
    }
}
