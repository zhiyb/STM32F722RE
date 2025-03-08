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

#define TXBD(count, addr) \
    (((count) << 16) | (0x0000ffff & ((uint32_t)(addr) - USB_DRD_PMAADDR)))
#define RXBD(blsize, blocks, count, addr) \
    (((blsize) << 31) | ((blocks) << 26) | ((count) << 16) | \
        (0x0000ffff & ((uint32_t)(addr) - USB_DRD_PMAADDR)))

#define TXRXDB_PTR(bd)      ((uint32_t *)(((bd) & 0x0000ffff) + USB_DRD_PMAADDR))
#define RXBD_COUNT(rxbd)    (((rxbd) >> 16) & 0x03ff)

#define CHEP(ch)            (*(&USB_DRD_FS->CHEP0R + ch))
#define CHEP_MASK(val)      (((val) & 0x017f070f) | 0x7e808080)

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
}

void usb_hw_ep_tx(uint8_t ep, void *data, uint32_t len, bool status_out)
{
    uint8_t ch = ep;    // TODO Not necessarily true

    // Configure SRAM buffer channel
    uint32_t *src = data;
    uint32_t *dst = TXRXDB_PTR(usb_sram->chep[ch].TXRXBD);  // TODO Double buffering support?
    for (uint32_t i = 0; i < (len + 3) / 4; i++)
        dst[i] = src[i];
    usb_sram->chep[ch].TXRXBD = TXBD(len, dst);

    // Enable endpoint channel for IN transfer
    uint32_t chep = CHEP(ch);
    CHEP(ch) = CHEP_MASK(chep) | (status_out ? USB_CHEP_KIND_Msk : 0) | ((~chep) & USB_CHEP_TX_STTX_Msk);
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

void usb_hw_ep_ctr_irq()
{
    uint32_t ep0r = CHEP(0);
    if (ep0r & USB_CHEP_VTRX_Msk) {
        if (ep0r & USB_CHEP_SETUP_Msk) {
            // SETUP
            // Defer to main thread
            event_push((event_t){.ep = 0, .ev = EventSetup});
            CHEP(0) = CHEP_MASK(ep0r) & ~USB_CHEP_VTRX_Msk;
        } else {
            dbg_bkpt();
        }
    } else if (ep0r & USB_CHEP_VTTX_Msk) {
        // IN complete, wait for SETUP/OUT
        CHEP(0) = CHEP_MASK(ep0r) & ~USB_CHEP_VTTX_Msk;
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
            uint32_t len = RXBD_COUNT(usb_sram->chep[0].RXTXBD);
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
