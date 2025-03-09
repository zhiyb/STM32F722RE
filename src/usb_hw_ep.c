#include "usb.h"
#include "stm32c0xx.h"
#include "semihosting.h"

// USB endpoint buffer allocation and management

typedef volatile struct {
    struct {
        uint32_t TXRXBD;
        uint32_t RXTXBD;
    } chep[8];
    struct {
        // Shared between TX & RX
        uint32_t buf[64 / 4];
    } ch0;
} usb_sram_t;

// For size checking
static usb_sram_t _sram __attribute__((section(".usbram"))) __attribute__((used));
static usb_sram_t * const usb_sram = (usb_sram_t *)USB_DRD_PMAADDR;

static const uint16_t ch_buf_size[8] = {
    sizeof(usb_sram->ch0.buf),
};

#define TXBD(count, addr) \
    (((count) << 16) | (0x0000ffff & ((uint32_t)(addr) - USB_DRD_PMAADDR)))
#define RXBD(blsize, blocks, count, addr) \
    (((blsize) << 31) | ((blocks) << 26) | ((count) << 16) | \
        (0x0000ffff & ((uint32_t)(addr) - USB_DRD_PMAADDR)))

#define TXRXDB_PTR(bd)      ((uint32_t *)(((bd) & 0x0000ffff) + USB_DRD_PMAADDR))
#define TXRXBD_COUNT(bd)    (((bd) >> 16) & 0x03ff)

#define CHEP(ch)            (*(&USB_DRD_FS->CHEP0R + ch))
#define CHEP_MASK(val)      (((val) & 0x017f070f) | 0x7e800000)

static volatile struct {
    const void *data;
    uint32_t len;
} tx_req;

void usb_hw_buf_init()
{
    // Configure channel 0 for control endpoint 0
    usb_sram->chep[0].TXRXBD = TXBD(0, &usb_sram->ch0.buf[0]);
    usb_sram->chep[0].RXTXBD = RXBD(1, sizeof(usb_sram->ch0.buf) / 32 - 1, 0, &usb_sram->ch0.buf[0]);
}

void usb_hw_ep0_init()
{
    // Use channel 0 for control endpoint 0
    // Ready for SETUP
    USB_DRD_FS->CHEP0R = (0b01 << USB_CHEP_UTYPE_Pos) | ((~USB_DRD_FS->CHEP0R) & USB_CHEP_RX_STRX_Msk);
    tx_req.len = 0;
}

static uint8_t ep_to_ch(uint8_t ep)
{
    return ep;
}

void usb_hw_ep_tx(uint8_t ep, const void *data, uint32_t len, bool status_out)
{
    uint8_t ch = ep_to_ch(ep);
    const uint16_t max_len = ch_buf_size[ch];

    if (len > max_len) {
        tx_req.data = data + max_len;
        tx_req.len = len - max_len;
        len = max_len;
    } else {
        tx_req.len = 0;
    }

    // Configure SRAM buffer channel
    uint32_t *dst = TXRXDB_PTR(usb_sram->chep[ch].TXRXBD);  // TODO Double buffering support?
    const uint32_t *src = data;
    for (uint32_t i = 0; i < (len + 3) / 4; i++)
        dst[i] = src[i];
    usb_sram->chep[ch].TXRXBD = TXBD(len, dst);

    // Enable endpoint channel for IN transfer
    uint32_t chep = CHEP(ch);
    CHEP(ch) = CHEP_MASK(chep) | (status_out ? USB_CHEP_KIND_Msk : 0) | ((~chep) & USB_CHEP_TX_STTX_Msk);
}

void usb_hw_ep_tx_stall(uint8_t ep)
{
    uint8_t ch = ep_to_ch(ep);

    // Enable endpoint channel for STALL transfer
    uint32_t chep = CHEP(ch);
    CHEP(ch) = (CHEP_MASK(chep) | (chep & USB_CHEP_TX_STTX_Msk)) ^ (0b01 << USB_CHEP_TX_STTX_Pos);
}

#define EVENT_QUEUE_SIZE    4

typedef enum {EventIdle, EventSetup} event_ev_t;

typedef struct {
    uint8_t ev;
    uint8_t ep;
} event_t;

static volatile struct {
    event_t queue[EVENT_QUEUE_SIZE];
    uint32_t read, write;
} event;

static void event_push(event_t ev)
{
    uint32_t ev_write = event.write;
    event.queue[ev_write] = ev;
    event.write = (ev_write + 1) % EVENT_QUEUE_SIZE;
}

static volatile uint16_t usb_daddr = 0;

void usb_hw_set_address(uint16_t addr)
{
    usb_daddr = addr;
}

void usb_hw_ep_ctr_irq()
{
    uint8_t ep = 0;
    uint8_t ch = ep_to_ch(ep);
    uint32_t chep = CHEP(ch);
    if (chep & USB_CHEP_VTRX_Msk) {
        if (chep & USB_CHEP_SETUP_Msk) {
            // SETUP
            // Defer to main thread
            event_push((event_t){.ep = ep, .ev = EventSetup});
            CHEP(ch) = CHEP_MASK(chep);
        } else if (TXRXBD_COUNT(usb_sram->chep[ch].RXTXBD) == 0) {
            // 0-length data, STATUS OUT
            // Wait for SETUP
            CHEP(ch) = CHEP_MASK(chep) | ((~USB_DRD_FS->CHEP0R) & USB_CHEP_RX_STRX_Msk);
        } else {
            dbg_bkpt();
        }
    } else if (chep & USB_CHEP_VTTX_Msk) {
        // IN complete
        if (tx_req.len) {
            // Continue with remaining data
            usb_hw_ep_tx(ep, tx_req.data, tx_req.len, true);
        } else {
            // Wait for SETUP/OUT
            CHEP(ch) = CHEP_MASK(chep) | ((~USB_DRD_FS->CHEP0R) & USB_CHEP_RX_STRX_Msk);
            if (!(chep & USB_CHEP_KIND_Msk)) {
                // STATUS IN, update device address
                USB_DRD_FS->DADDR = USB_DADDR_EF_Msk | usb_daddr;
            }
        }
    } else {
        dbg_bkpt();
    }
}

void usb_hw_ep_process()
{
    uint32_t ev_write = event.write;
    uint32_t ev_read = event.read;
    while (ev_write != ev_read) {
        event_t ev = event.queue[ev_read];
        ev_read = (ev_read + 1) % EVENT_QUEUE_SIZE;

        switch (ev.ev) {
        case EventSetup: {
            // Data from USBRAM can only be accessed by word
            // Copy to local buffer for easy access
            uint32_t buf[64 / 4];
            uint32_t *data = &usb_sram->ch0.buf[0];
            uint32_t len = TXRXBD_COUNT(usb_sram->chep[0].RXTXBD);
            for (uint32_t i = 0; i < (len + 3) / 4; i++)
                buf[i] = data[i];
            usb_ep0_setup(&buf[0], len);
            break;
        }

        default:
            dbg_bkpt();
        }
    }
    event.read = ev_read;
}
