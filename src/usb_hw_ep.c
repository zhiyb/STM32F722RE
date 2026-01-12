#include "stm32f7xx.h"
#include "semihosting.h"
#include "macros.h"
#include "log.h"
#include "usb.h"
#include "usb_hw.h"
#include "usb_internal.h"

static uint8_t usb_hw_ep_fifo_alloc(usb_if_t usb_if, uint32_t size)
{
    usb_t *usb = &usb_ifs[usb_if];
    const usb_hw_info_t *hw = &usb_hw_ifs[usb_if];
    USB_OTG_GlobalTypeDef *hw_g = HW_G(hw->base);
    USB_OTG_DeviceTypeDef *hw_dev = HW_DEV(hw->base);

    uint8_t num = usb->hw.fifo_num;
    uint32_t addr = usb->hw.fifo_top;
    size /= 4;
    usb->hw.fifo_num += 1;
    usb->hw.fifo_top += size;
    if (usb->hw.fifo_top > usb->hw_info->ram_size / 4)
        PANIC("USB RAM out of space");

    switch (num) {
    case 0:		// Rx FIFO
        hw_g->GRXFSIZ = size << USB_OTG_GRXFSIZ_RXFD_Pos;
        break;
    case 1:		// Tx endpoint 0
        hw_g->DIEPTXF0_HNPTXFSIZ = DIEPTXF(addr, size);
        break;
    default:	// Other Tx endpoints
        hw_g->DIEPTXF[num - 2] = DIEPTXF(addr, size);
    }
    return num - 1;
}

void usb_hw_ep_init(usb_if_t usb_if)
{
    usb_t *usb = &usb_ifs[usb_if];
    const usb_hw_info_t *hw = &usb_hw_ifs[usb_if];
    USB_OTG_GlobalTypeDef *hw_g = HW_G(hw->base);
    USB_OTG_DeviceTypeDef *hw_dev = HW_DEV(hw->base);

    // Re-allocate FIFO
    usb->hw.fifo_num = 0;
    usb->hw.fifo_top = 0;

    // Allocate FIFO for global RX
    usb_hw_ep_fifo_alloc(usb_if, usb_if == UsbIfFs ? 512 : 1024);

    hw_dev->DAINTMSK = 0;
    uint32_t daintmsk = 0;

    // IN endpoint 0
    uint8_t ch = UsbEp0Ctrl;
    uint32_t fifo = usb_hw_ep_fifo_alloc(usb_if, 64 * 2);
    USB_OTG_INEndpointTypeDef *hw_ep_in = HW_EP_IN(hw->base, 0);
    uint32_t epsize = usb_if == UsbIfHs ? 64 : 0;
    usb->ep[0].in.max_size = 64;
    // uint32_t epdis = hw_ep_in->DIEPCTL & USB_OTG_DIEPCTL_EPENA_Msk ? USB_OTG_DIEPCTL_EPDIS_Msk : 0;
    hw_ep_in->DIEPCTL = (fifo << USB_OTG_DIEPCTL_TXFNUM_Pos) | (epsize << USB_OTG_DIEPCTL_MPSIZ_Pos);
    daintmsk |= 1 << (USB_OTG_DAINTMSK_IEPM_Pos + 0);

    // OUT endpoint 0
    // USB_OTG_OUTEndpointTypeDef *hw_ep_out = HW_EP_OUT(hw->base, 0);
    // epdis = hw_ep_out->DOEPCTL & USB_OTG_DOEPCTL_EPENA_Msk ? USB_OTG_DOEPCTL_EPDIS_Msk : 0;
    usb->ep[0].out.max_size = 64;
    daintmsk |= 1 << (USB_OTG_DAINTMSK_OEPM_Pos + 0);

    usb_ep0_init(usb_if);

    // Interrupt and event masks
    hw_dev->DAINTMSK = daintmsk;
    // OUT: Transfer complete, setup done, status phase
    hw_dev->DOEPMSK = USB_OTG_DOEPMSK_XFRCM_Msk |
        USB_OTG_DOEPMSK_STUPM_Msk | USB_OTG_DOEPMSK_OTEPSPRM_Msk /* STSPHSRXM */;
    // IN: Transfer complete, timeout
    hw_dev->DIEPMSK = USB_OTG_DIEPMSK_XFRCM_Msk | USB_OTG_DIEPMSK_TOM_Msk;
}

void usb_hw_ep_out(usb_if_t usb_if, uint32_t ep, void *p, uint32_t setup, uint32_t pkt, uint32_t size)
{
    usb_t *usb = &usb_ifs[usb_if];
    const usb_hw_info_t *hw = &usb_hw_ifs[usb_if];
    USB_OTG_OUTEndpointTypeDef *hw_ep_out = HW_EP_OUT(hw->base, ep);
    // Set transfer size to maximum packet size to be interrupted at the end of each packet
    if (size == 0)
        size = usb->ep[ep].out.max_size;
    if (hw->use_dma)
        hw_ep_out->DOEPDMA = (uint32_t)p;
    else
        usb->ep[ep].out.p = p;
    log_push(LogUSB_Out, size);
    hw_ep_out->DOEPTSIZ = (setup << USB_OTG_DOEPTSIZ_STUPCNT_Pos) | (pkt << USB_OTG_DOEPTSIZ_PKTCNT_Pos) |
        (size << USB_OTG_DOEPTSIZ_XFRSIZ_Pos);
    hw_ep_out->DOEPCTL = (hw_ep_out->DOEPCTL & (USB_OTG_DOEPCTL_MPSIZ_Msk | USB_OTG_DOEPCTL_EPTYP_Msk)) |
        USB_OTG_DOEPCTL_USBAEP_Msk | USB_OTG_DOEPCTL_EPENA_Msk | USB_OTG_DOEPCTL_CNAK_Msk;
}

void usb_hw_ep_in_continue(usb_if_t usb_if, uint8_t ep)
{
    const usb_hw_info_t *hw = &usb_hw_ifs[usb_if];
    USB_OTG_INEndpointTypeDef *hw_ep_in = HW_EP_IN(hw->base, ep);
    // uint32_t act_mask = USB_OTG_DIEPCTL_EPENA_Msk | USB_OTG_DIEPCTL_USBAEP_Msk;
    // if ((hw_ep_in->DIEPCTL & act_mask) == act_mask)
    //     PANIC("EP not idle");
    if (hw_ep_in->DIEPCTL & USB_OTG_DIEPCTL_EPENA_Msk)
        PANIC("EP not idle");

    usb_t *usb = &usb_ifs[usb_if];
    if (!usb->ep[ep].in.pkts)
        return;
    usb->ep[ep].in.pkts -= 1;

    uint32_t max_len = usb->ep[ep].in.max_size;
    uint32_t pkt_len = usb->ep[ep].in.len;
    pkt_len = pkt_len >= max_len ? max_len : pkt_len;
    usb->ep[ep].in.len -= pkt_len;
    log_push(LogUSB_InContinue, pkt_len);

    hw_ep_in->DIEPTSIZ = (1 << USB_OTG_DIEPTSIZ_PKTCNT_Pos) | (pkt_len << USB_OTG_DIEPTSIZ_XFRSIZ_Pos);
    hw_ep_in->DIEPCTL = ((hw_ep_in->DIEPCTL) & (USB_OTG_DIEPCTL_TXFNUM_Msk | USB_OTG_DIEPCTL_MPSIZ_Msk |
            USB_OTG_DIEPCTL_EPTYP_Msk)) | USB_OTG_DIEPCTL_CNAK_Msk |
            USB_OTG_DIEPCTL_EPENA_Msk | USB_OTG_DIEPCTL_USBAEP_Msk;

    uint32_t *p = usb->ep[ep].in.p;
    usb->ep[ep].in.p += pkt_len;
    for (uint32_t i = 0; i < (pkt_len + 3) / 4; i++)
        *HW_EP_FIFO(hw->base, ep) = *p++;
}

void usb_hw_ep_in(usb_if_t usb_if, uint8_t ep, const void *data, uint32_t len, bool short_data)
{
    usb_t *usb = &usb_ifs[usb_if];
    uint32_t max_len = usb->ep[ep].in.max_size;
    if (usb->ep[ep].in.pkts != 0)
        PANIC("EP not idle");
    usb->ep[ep].in.p = data;
    usb->ep[ep].in.len = len;
    usb->ep[ep].in.pkts = (len + max_len - (short_data ? 0 : 1)) / max_len;
    log_push(LogUSB_In, len);
    usb_hw_ep_in_continue(usb_if, ep);
}

void usb_hw_ep_in_stall(usb_if_t usb_if, uint8_t ep)
{
    const usb_hw_info_t *hw = &usb_hw_ifs[usb_if];
    USB_OTG_INEndpointTypeDef *hw_ep_in = HW_EP_IN(hw->base, ep);
    if (hw_ep_in->DIEPCTL & USB_OTG_DIEPCTL_EPENA_Msk)
        PANIC("EP not idle");
    hw_ep_in->DIEPCTL = ((hw_ep_in->DIEPCTL) & (USB_OTG_DIEPCTL_TXFNUM_Msk | USB_OTG_DIEPCTL_MPSIZ_Msk |
            USB_OTG_DIEPCTL_EPTYP_Msk)) | USB_OTG_DIEPCTL_STALL_Msk | USB_OTG_DIEPCTL_USBAEP_Msk;
}

// void usb_hw_ep_tx_stall(uint8_t ep)
// {
//     uint8_t ch = ep_to_tx_ch(ep);
//     uint32_t chep = CHEP(ch);
//     CHEP(ch) = CHEP_MASK(chep) | CHEP_TX_STALL(chep);
// }

// void usb_hw_ep_tx(uint8_t ep, const void *data, uint32_t len, bool status_out)
// {
//     uint8_t ch = ep_to_tx_ch(ep);
//     const uint16_t max_len = ch_buf_size[ch][Tx];

//     if (len > max_len) {
//         txrx_req[ch].data = (void *)data + max_len;
//         txrx_req[ch].len = len - max_len;
//         len = max_len;
//     } else {
//         txrx_req[ch].len = 0;
//     }

//     // Configure SRAM buffer channel
//     uint32_t *dst = TXRXDB_PTR(usb_sram->chep[ch].TXRXBD);  // TODO Double buffering support?
//     const uint32_t *src = data;
//     for (uint32_t i = 0; i < (len + 3) / 4; i++)
//         dst[i] = src[i];
//     usb_sram->chep[ch].TXRXBD = TXBD(len, dst);

//     // Enable endpoint channel for IN transfer
//     uint32_t chep = CHEP(ch);
//     // log_push(LogUSB_CHEP_TX, chep);
//     // log_push(LogUSB_CHEP_LEN, len);
//     CHEP(ch) = CHEP_MASK(chep) | (status_out ? USB_CHEP_KIND_Msk : 0) | CHEP_TX_VALID(chep);
// }

// #define TXBD(count, addr) \
//     (((count) << 16) | (0x0000ffff & ((uint32_t)(addr) - USB_DRD_PMAADDR)))
// #define RXBD(blsize, blocks, count, addr) \
//     (((blsize) << 31) | ((blocks) << 26) | ((count) << 16) | \
//         (0x0000ffff & ((uint32_t)(addr) - USB_DRD_PMAADDR)))

// #define TXRXDB_PTR(bd)      ((uint32_t *)(((bd) & 0x0000ffff) + USB_DRD_PMAADDR))
// #define TXRXBD_COUNT(bd)    (((bd) >> 16) & 0x03ff)

// #define CHEP(ch)            (*(&USB_DRD_FS->CHEP0R + ch))
// #define CHEP_MASK(val)      (((val) & 0x017f070f) | 0x7e808080)

// #define CHEP_RX_DISABLED(v) ((v) & USB_CHEP_RX_STRX_Msk)
// #define CHEP_RX_STALL(v)    (((v) & USB_CHEP_RX_STRX_Msk) ^ (0b01 << USB_CHEP_RX_STRX_Pos))
// #define CHEP_RX_NAK(v)      (((v) & USB_CHEP_RX_STRX_Msk) ^ (0b10 << USB_CHEP_RX_STRX_Pos))
// #define CHEP_RX_VALID(v)    ((~(v)) & USB_CHEP_RX_STRX_Msk)

// #define CHEP_TX_DISABLED(v) ((v) & USB_CHEP_TX_STTX_Msk)
// #define CHEP_TX_STALL(v)    (((v) & USB_CHEP_TX_STTX_Msk) ^ (0b01 << USB_CHEP_TX_STTX_Pos))
// #define CHEP_TX_NAK(v)      (((v) & USB_CHEP_TX_STTX_Msk) ^ (0b10 << USB_CHEP_TX_STTX_Pos))
// #define CHEP_TX_VALID(v)    ((~(v)) & USB_CHEP_TX_STTX_Msk)

// // USB endpoint buffer allocation and management

// static const uint8_t map_ch_to_ep[8] = {0, 1,};

// typedef volatile struct {
//     struct {
//         uint32_t TXRXBD;
//         uint32_t RXTXBD;
//     } chep[8];
//     struct {
//         // For enpoint 0 control
//         // Shared between TX & RX
//         uint32_t buf[64 / 4];
//     } ch0;
//     struct {
//         // For IN endpoint 1 BT HCI Events
//         uint32_t rx_buf[0 / 4];
//         uint32_t tx_buf[64 / 4];
//     } ch1;
//     struct {
//         // For OUT endpoint 2 BT ACL Data
//         uint32_t rx_buf[2][64 / 4];
//     } ch2;
//     struct {
//         // For OUT endpoint 3 BT Voice
//         uint32_t rx_buf[2][64 / 4];
//     } ch3;
//     struct {
//         // For endpoint 4 HID
//         // For endpoint 4 CDC Comm
//         uint32_t rx_buf[64 / 4];
//         uint32_t tx_buf[64 / 4];
//     } ch4;
//     struct {
//         // For endpoint 5 CDC Data
//         uint32_t rx_buf[64 / 4];
//         uint32_t tx_buf[64 / 4];
//     } ch5;
//     struct {
//         // For IN endpoint 2 BT ACL Data
//         uint32_t tx_buf[2][64 / 4];
//     } ch6;
//     struct {
//         // For IN endpoint 3 BT Voice
//         uint32_t tx_buf[2][64 / 4];
//     } ch7;
// } usb_sram_t;

// // For size checking
// static usb_sram_t _usb_sram __attribute__((section(".usbram"))) __attribute__((used));
// static usb_sram_t * const usb_sram = (usb_sram_t *)USB_DRD_PMAADDR;

// enum {Tx = 0, Rx = 1};

// static const uint16_t ch_buf_size[8][2] = {
//     // IN (TX), OUT (RX)
//     {sizeof(usb_sram->ch0.buf), sizeof(usb_sram->ch0.buf)},
//     {sizeof(usb_sram->ch1.tx_buf), sizeof(usb_sram->ch1.rx_buf)},
//     {0, sizeof(usb_sram->ch2.rx_buf[0])},
//     {0, sizeof(usb_sram->ch3.rx_buf[0])},
//     {sizeof(usb_sram->ch4.tx_buf), sizeof(usb_sram->ch4.rx_buf)},
//     {sizeof(usb_sram->ch5.tx_buf), sizeof(usb_sram->ch5.rx_buf)},
//     {sizeof(usb_sram->ch6.tx_buf[0]), 0},
//     {sizeof(usb_sram->ch7.tx_buf[0]), 0},
// };

// // In-progress TX/RX requests
// static struct {
//     volatile void * volatile data;
//     volatile uint32_t len;
//     volatile uint8_t db_valid[2];
//     volatile uint8_t skip;
// } txrx_req[8];          // Channel index

// static volatile struct {
//     setup_t setup;
//     uint8_t data[256];  // DATA OUT buffer
// } ctrl_buf ALIGNED(4);

// // Event queue for deferring IRQ events to main thread
// #define EVENT_QUEUE_SIZE    64

// typedef enum {
//     EventSetup,
//     EventOut,
//     EventIn,
// } usb_hw_event_ev_t;

// typedef struct {
//     uint8_t type;
//     uint8_t ch;
// } usb_hw_event_data_t;

// typedef struct {
//     usb_hw_event_data_t event[EVENT_QUEUE_SIZE];
//     volatile uint8_t wptr, rptr;
// } usb_hw_event_t;

// static volatile usb_hw_event_t usb_hw_event;

// static void event_push_irq(usb_hw_event_ev_t ev, uint8_t ch)
// {
//     uint8_t wptr = usb_hw_event.wptr;
//     usb_hw_event.event[wptr] = (usb_hw_event_data_t){ev, ch};
//     usb_hw_event.wptr = (wptr + 1) % EVENT_QUEUE_SIZE;
// }

// void usb_hw_ep_init()
// {
//     // Clear pending events
//     usb_hw_event.wptr = 0;
//     usb_hw_event.rptr = 0;

//     // Configure channel 0 for control endpoint 0
//     uint8_t ch = UsbEp0Ctrl;
//     usb_sram->chep[ch].TXRXBD = TXBD(0, &usb_sram->ch0.buf[0]);
//     usb_sram->chep[ch].RXTXBD = RXBD(1, ch_buf_size[ch][Rx] / 32 - 1, 0, &usb_sram->ch0.buf[0]);
//     txrx_req[ch].len = 0;
//     ctrl_buf.setup.wLength = 0;
//     // Ready for SETUP
//     uint32_t chep = CHEP(ch);
//     CHEP(ch) = (0b01 << USB_CHEP_UTYPE_Pos) | CHEP_RX_VALID(chep) | CHEP_TX_NAK(chep) | ch;

//     // Configure channel 1 for BT HCL Events interrupt IN endpoint
//     ch = UsbEpBtHciEvents;
//     usb_sram->chep[ch].TXRXBD = TXBD(0, &usb_sram->ch1.tx_buf[0]);
//     usb_sram->chep[ch].RXTXBD = RXBD(1, ch_buf_size[ch][Rx] / 32 - 1, 0, &usb_sram->ch1.rx_buf[0]);
//     txrx_req[ch].len = 0;
//     // Send NAK for now
//     chep = CHEP(ch);
//     CHEP(ch) = (0b11 << USB_CHEP_UTYPE_Pos) | CHEP_RX_DISABLED(chep) | CHEP_TX_NAK(chep) | ch;

//     // Configure channel 2 for BT ACL Data bulk OUT endpoint, with double buffering
//     ch = UsbEpBtACLData;
//     usb_sram->chep[ch].TXRXBD = RXBD(1, ch_buf_size[ch][Rx] / 32 - 1, 0, &usb_sram->ch2.rx_buf[1][0]);
//     usb_sram->chep[ch].RXTXBD = RXBD(1, ch_buf_size[ch][Rx] / 32 - 1, 0, &usb_sram->ch2.rx_buf[0][0]);
//     txrx_req[ch].len = 0;
//     // Disable to match first state
//     chep = CHEP(ch);
//     CHEP(ch) = (0b00 << USB_CHEP_UTYPE_Pos) | USB_CHEP_KIND_Msk | CHEP_RX_DISABLED(chep) | CHEP_TX_DISABLED(chep) |
//         (chep & (USB_CHEP_DTOG_TX_Msk | USB_CHEP_DTOG_RX_Msk)) | ch;
//     // Ready for OUT
//     chep = CHEP(ch);
//     CHEP(ch) = (0b00 << USB_CHEP_UTYPE_Pos) | USB_CHEP_KIND_Msk | CHEP_RX_VALID(chep) | CHEP_TX_DISABLED(chep) |
//         (chep & (USB_CHEP_DTOG_TX_Msk | USB_CHEP_DTOG_RX_Msk)) | ch;

//     // Configure channel 6 for BT ACL Data bulk IN endpoint, with double buffering
//     ch = UsbEpBtACLDataIn;
//     usb_sram->chep[ch].TXRXBD = RXBD(1, ch_buf_size[ch][Rx] / 32 - 1, 0, &usb_sram->ch6.tx_buf[0][0]);
//     usb_sram->chep[ch].RXTXBD = RXBD(1, ch_buf_size[ch][Rx] / 32 - 1, 0, &usb_sram->ch6.tx_buf[1][0]);
//     txrx_req[ch].len = 0;
//     // Send null data to get double buffering logic under control
//     txrx_req[ch].db_valid[0] = 1;
//     txrx_req[ch].db_valid[1] = 1;
//     txrx_req[ch].skip = 2;
//     // Disable to reset first
//     chep = CHEP(ch);
//     CHEP(ch) = (0b00 << USB_CHEP_UTYPE_Pos) | USB_CHEP_KIND_Msk | CHEP_RX_DISABLED(chep) | CHEP_TX_DISABLED(chep) |
//         (chep & (USB_CHEP_DTOG_TX_Msk | USB_CHEP_DTOG_RX_Msk)) | UsbEpBtACLData;
//     // Null data available for TX
//     chep = CHEP(ch);
//     CHEP(ch) = (0b00 << USB_CHEP_UTYPE_Pos) | USB_CHEP_KIND_Msk | CHEP_RX_DISABLED(chep) | CHEP_TX_VALID(chep) |
//         (chep & (USB_CHEP_DTOG_TX_Msk | USB_CHEP_DTOG_RX_Msk)) | UsbEpBtACLData;

//     // Configure channel 3 for BT Voice isochronous OUT endpoint
//     ch = UsbEpBtVoice;
//     usb_sram->chep[ch].TXRXBD = RXBD(1, ch_buf_size[ch][Rx] / 32 - 1, 0, &usb_sram->ch3.rx_buf[1][0]);
//     usb_sram->chep[ch].RXTXBD = RXBD(1, ch_buf_size[ch][Rx] / 32 - 1, 0, &usb_sram->ch3.rx_buf[0][0]);
//     txrx_req[ch].len = 0;
//     // Disabled for now
//     chep = CHEP(ch);
//     CHEP(ch) = (0b00 << USB_CHEP_UTYPE_Pos) | CHEP_RX_DISABLED(chep) | CHEP_TX_DISABLED(chep) |
//         (chep & (USB_CHEP_DTOG_TX_Msk | USB_CHEP_DTOG_RX_Msk)) | ch;

//     // Configure channel 7 for BT Voice isochronous IN endpoint
//     ch = UsbEpBtVoiceIn;
//     usb_sram->chep[ch].TXRXBD = RXBD(1, ch_buf_size[ch][Rx] / 32 - 1, 0, &usb_sram->ch7.tx_buf[0][0]);
//     usb_sram->chep[ch].RXTXBD = RXBD(1, ch_buf_size[ch][Rx] / 32 - 1, 0, &usb_sram->ch7.tx_buf[1][0]);
//     txrx_req[ch].len = 0;
//     txrx_req[ch].db_valid[0] = 0;
//     txrx_req[ch].db_valid[1] = 0;
//     // Disabled for now
//     chep = CHEP(ch);
//     CHEP(ch) = (0b00 << USB_CHEP_UTYPE_Pos) | CHEP_RX_DISABLED(chep) | CHEP_TX_DISABLED(chep) |
//         (chep & (USB_CHEP_DTOG_TX_Msk | USB_CHEP_DTOG_RX_Msk)) | UsbEpBtVoice;

//     // Auxiliary endpoints

// #if USB_ALT_IF == USB_ALT_IF_HID
//     // Configure channel 4 for HID interrupt endpoint
//     ch = UsbEpHid;
//     usb_sram->chep[ch].TXRXBD = TXBD(0, &usb_sram->ch4.tx_buf[0]);
//     usb_sram->chep[ch].RXTXBD = RXBD(1, ch_buf_size[ch][Rx] / 32 - 1, 0, &usb_sram->ch4.rx_buf[0]);
//     txrx_req[ch].len = 0;
//     // Send NAK for now
//     chep = CHEP(ch);
//     CHEP(ch) = (0b11 << USB_CHEP_UTYPE_Pos) | CHEP_RX_NAK(chep) | CHEP_TX_NAK(chep) | ch;

// #elif USB_ALT_IF == USB_ALT_IF_CDC
//     // Configure channel 4 for CDC Comm interrupt endpoint
//     ch = UsbEpCDCComm;
//     usb_sram->chep[ch].TXRXBD = TXBD(0, &usb_sram->ch4.tx_buf[0]);
//     usb_sram->chep[ch].RXTXBD = RXBD(1, ch_buf_size[ch][Rx] / 32 - 1, 0, &usb_sram->ch4.rx_buf[0]);
//     txrx_req[ch].len = 0;
//     // Send NAK for now
//     chep = CHEP(ch);
//     CHEP(ch) = (0b11 << USB_CHEP_UTYPE_Pos) | CHEP_RX_DISABLED(chep) | CHEP_TX_NAK(chep) | ch;

//     // Configure channel 5 for CDC Data bulk endpoint
//     ch = UsbEpCDCData;
//     usb_sram->chep[ch].TXRXBD = TXBD(0, &usb_sram->ch5.tx_buf[0]);
//     usb_sram->chep[ch].RXTXBD = RXBD(1, ch_buf_size[ch][Rx] / 32 - 1, 0, &usb_sram->ch5.rx_buf[0]);
//     txrx_req[ch].len = 0;
//     // OUT ready
//     chep = CHEP(ch);
//     CHEP(ch) = (0b00 << USB_CHEP_UTYPE_Pos) | CHEP_RX_VALID(chep) | CHEP_TX_NAK(chep) | ch;
// #endif
// }

// static inline uint8_t ep_to_tx_ch(uint8_t ep)
// {
// #if 0
//     if (ep == UsbEpBtACLData)
//         return UsbEpBtACLDataIn;
//     else if (ep == UsbEpBtVoice)
//         return UsbEpBtVoiceIn;
// #endif
//     return ep;
// }

// static inline uint8_t ep_to_rx_ch(uint8_t ep)
// {
//     return ep;
// }

// bool usb_hw_ep_tx_db_available(uint8_t ep)
// {
//     // TX with double buffering
//     uint8_t ch = ep_to_tx_ch(ep);
//     uint32_t chep = CHEP(ch);
//     uint8_t sw_buf = !!(chep & USB_CHEP_DTOG_RX_Msk);
//     return !txrx_req[ch].db_valid[sw_buf];
// }

// void usb_hw_ep_tx_db(uint8_t ep, const void *data, uint32_t len)
// {
//     // TX with double buffering
//     uint8_t ch = ep_to_tx_ch(ep);
//     const uint16_t max_len = ch_buf_size[ch][Tx];

//     // Not actually used
//     if (len > max_len) {
//         txrx_req[ch].data = (void *)data + max_len;
//         txrx_req[ch].len = len - max_len;
//         len = max_len;
//     } else {
//         txrx_req[ch].len = 0;
//     }

//     // Find next SW buffer
//     uint32_t chep = CHEP(ch);
//     bool sw_buf = !!(chep & USB_CHEP_DTOG_RX_Msk);
//     volatile uint32_t *bd = sw_buf ? &usb_sram->chep[ch].RXTXBD : &usb_sram->chep[ch].TXRXBD;

//     // Copy to USB SRAM buffer
//     uint32_t *dst = TXRXDB_PTR(*bd);
//     const uint32_t *src = data;
//     for (uint32_t i = 0; i < (len + 3) / 4; i++)
//         dst[i] = src[i];
//     *bd = TXBD(len, dst);
//     txrx_req[ch].db_valid[sw_buf] = 1;

//     // Tell hardware that buffer is good to use
//     // log_push(LogUSB_CHEP_TX, chep);
//     // log_push(LogUSB_CHEP_LEN, len);
//     CHEP(ch) = CHEP_MASK(chep) | USB_CHEP_DTOG_RX_Msk;
// }

// void usb_hw_ep_tx_nak(uint8_t ep)
// {
//     uint8_t ch = ep_to_tx_ch(ep);
//     uint32_t chep = CHEP(ch);
//     CHEP(ch) = CHEP_MASK(chep) | CHEP_TX_NAK(chep);
// }

// uint32_t *usb_hw_ep_tx_buffer(uint8_t ep, uint16_t *len)
// {
//     uint8_t ch = ep_to_tx_ch(ep);
//     if (len)
//         *len = ch_buf_size[ch][Tx];
//     return TXRXDB_PTR(usb_sram->chep[ch].TXRXBD);
// }

// usb_endpoint_status_t usb_hw_ep_tx_status(uint8_t ep)
// {
//     uint32_t chep = CHEP(ep_to_tx_ch(ep));
//     return (chep >> USB_CHEP_TX_STTX_Pos) & 0b11;
// }

// usb_endpoint_status_t usb_hw_ep_rx_status(uint8_t ep)
// {
//     uint32_t chep = CHEP(ep_to_rx_ch(ep));
//     return (chep >> USB_CHEP_RX_STRX_Pos) & 0b11;
// }


// static volatile uint16_t usb_daddr = 0;

// void usb_hw_set_address(uint16_t addr)
// {
//     usb_daddr = addr;
// }

// void usb_hw_ep_ctr_irq(uint8_t idn)
// {
//     uint8_t ch = idn & 0x0f;
//     uint32_t chep = CHEP(ch);
//     uint8_t ep = chep & 0x0f;
//     log_push(LogUSB_CHEP, chep);

//     if (idn & 0x10) {
//         // RX complete, SETUP or OUT
//         if (!(chep & USB_CHEP_VTRX_Msk))
//             return;

//         usb_hw_event_ev_t ev = EventOut;
//         switch (chep & USB_CHEP_UTYPE_Msk) {
//         case 0b01 << USB_CHEP_UTYPE_Pos:    // Control
//             ev = EventSetup;
//             if (chep & USB_CHEP_SETUP_Msk) {
//                 // SETUP
//                 uint32_t bd = usb_sram->chep[ch].RXTXBD;
//                 uint32_t len = TXRXBD_COUNT(bd);
//                 if (len != 8) {
//                     // Invalid setup packet size, ignore
//                     CHEP(ch) = (CHEP_MASK(chep) & ~USB_CHEP_VTRX_Msk) | CHEP_RX_VALID(chep);
//                 } else {
//                     // Data from USBRAM can only be accessed by word
//                     // Copy to local buffer for easy access
//                     uint32_t *dst = (uint32_t *)&ctrl_buf.setup;
//                     uint32_t *src = TXRXDB_PTR(bd);
//                     dst[0] = src[0];
//                     dst[1] = src[1];

//                     setup_t *setup = (setup_t *)&ctrl_buf.setup;
//                     if (((setup->bmRequestType & 0x80) == 0) && (setup->wLength != 0)) {
//                         // Continue receiving for DATA OUT
//                         txrx_req[ch].data = (void volatile *)&ctrl_buf.data[0];
//                         if (setup->wLength > sizeof(ctrl_buf.data)) {
//                             DBG_BKPT("Insufficient buffer size");
//                             txrx_req[ch].len = 0;
//                         } else {
//                             txrx_req[ch].len = setup->wLength;
//                         }
//                         CHEP(ch) = (CHEP_MASK(chep) & ~(USB_CHEP_KIND_Msk | USB_CHEP_VTRX_Msk)) | CHEP_RX_VALID(chep);

//                     } else {
//                         // SETUP complete, defer to main thread
//                         event_push_irq(EventSetup, ch);
//                         CHEP(ch) = CHEP_MASK(chep) & ~USB_CHEP_VTRX_Msk;
//                     }
//                 }
//                 break;

//             } else if (TXRXBD_COUNT(usb_sram->chep[ch].RXTXBD) == 0) {
//                 // 0-length data, STATUS OUT
//                 // Wait for SETUP
//                 CHEP(ch) = (CHEP_MASK(chep) & ~USB_CHEP_VTRX_Msk) | CHEP_RX_VALID(chep);
//                 break;
//             }

//             // DATA OUT, fall-through

//         case 0b11 << USB_CHEP_UTYPE_Pos: {  // Interrupt (or control)
//             // DATA OUT
//             uint32_t bd = usb_sram->chep[ch].RXTXBD;
//             uint16_t len = TXRXBD_COUNT(bd);
//             uint32_t buf_len = txrx_req[ch].len;
//             len = len < buf_len ? len : buf_len;
//             // Copy to data buffer
//             uint32_t *dst = (uint32_t *)txrx_req[ch].data;
//             uint32_t *src = TXRXDB_PTR(bd);
//             for (uint16_t i = 0; i < (len + 3) / 4; i++)
//                 dst[i] = src[i];
//             buf_len -= len;
//             txrx_req[ch].data = dst + len / 4;
//             txrx_req[ch].len = buf_len;

//             if (buf_len == 0) {
//                 // DATA OUT complete, defer to main thread
//                 event_push_irq(ev, ch);
//                 // Keep NAK, wait for main thread
//                 CHEP(ch) = CHEP_MASK(chep) & ~USB_CHEP_VTRX_Msk;
//             } else {
//                 // More data to be received
//                 CHEP(ch) = (CHEP_MASK(chep) & ~USB_CHEP_VTRX_Msk) | CHEP_RX_VALID(chep);
//             }
//             break;
//         }

//         case 0b00 << USB_CHEP_UTYPE_Pos:    // Bulk
//             // Send OUT event to main thread
//             event_push_irq(EventOut, ch);
//             CHEP(ch) = CHEP_MASK(chep) & ~USB_CHEP_VTRX_Msk;
//             break;

//         default:
//             DBG_BKPT("Unhandled endpoint type");
//         }

//     } else {
//         // DATA IN complete
//         if (!(chep & USB_CHEP_VTTX_Msk))
//             return;

//         switch (chep & USB_CHEP_UTYPE_Msk) {
//         case 0b01 << USB_CHEP_UTYPE_Pos:    // Control
//             if (txrx_req[ch].len) {
//                 // Continue with remaining data
//                 usb_hw_ep_tx(ep, (void *)txrx_req[ch].data, txrx_req[ch].len, true);
//                 CHEP(ch) = CHEP_MASK(chep) & ~USB_CHEP_VTTX_Msk;
//             } else {
//                 // Wait for SETUP / STATUS OUT
//                 CHEP(ch) = (CHEP_MASK(chep) & ~USB_CHEP_VTTX_Msk) | CHEP_RX_VALID(chep);
//                 if (!(chep & USB_CHEP_KIND_Msk)) {
//                     // STATUS IN, update device address
//                     USB_DRD_FS->DADDR = USB_DADDR_EF_Msk | usb_daddr;
//                 }
//             }
//             break;

//         case 0b11 << USB_CHEP_UTYPE_Pos:    // Interrupt
//             if (txrx_req[ch].len) {
//                 // Continue with remaining data
//                 usb_hw_ep_tx(ep, (void *)txrx_req[ch].data, txrx_req[ch].len, true);
//             } else {
//                 // No more data
//                 event_push_irq(EventIn, ch);
//             }
//             CHEP(ch) = CHEP_MASK(chep) & ~USB_CHEP_VTTX_Msk;
//             break;

//         case 0b00 << USB_CHEP_UTYPE_Pos:    // Bulk
//             if (chep & USB_CHEP_KIND_Msk) {
//                 // Double buffering enabled
//                 bool dtog = !!(chep & USB_CHEP_DTOG_TX_Msk);
//                 txrx_req[ch].db_valid[!dtog] = 0;
//             }

//             // Send IN complete event to main thread
//             event_push_irq(EventIn, ch);
//             CHEP(ch) = CHEP_MASK(chep) & ~USB_CHEP_VTTX_Msk;
//             break;

//         default:
//             DBG_BKPT("Unhandled endpoint type");
//         }
//     }
// }

// void usb_hw_ep_process()
// {
//     uint8_t rptr = usb_hw_event.rptr;
//     if (rptr == usb_hw_event.wptr)
//         return;

//     usb_hw_event_data_t event = usb_hw_event.event[rptr];
//     usb_hw_event_ev_t ev = event.type;
//     uint8_t ch = event.ch;

//     switch (ev) {
//     case EventSetup:
//         usb_ep0_setup((setup_t *)&ctrl_buf.setup);
//         break;

//     case EventOut: {
//         uint32_t chep = CHEP(ch);
//         if ((chep & USB_CHEP_UTYPE_Msk) == (0b10 << USB_CHEP_UTYPE_Pos) ||
//             (chep & (USB_CHEP_UTYPE_Msk | USB_CHEP_KIND_Msk)) == USB_CHEP_KIND_Msk) {
//             // Double buffering enabled
//             bool sw_buf = !!(chep & USB_CHEP_DTOG_TX_Msk);
//             uint32_t bd = sw_buf ? usb_sram->chep[ch].RXTXBD : usb_sram->chep[ch].TXRXBD;
//             uint32_t *data = TXRXDB_PTR(bd);
//             uint16_t len = TXRXBD_COUNT(bd);
//             bool rx_valid = false;
//             switch (ch) {
//             case UsbEpBtACLData:
//                 rx_valid = bt_hci_usb_acl_tx(data, len);
//                 break;
//             default:
//                 DBG_BKPT("Unknown endpoint");
//             }
//             if (rx_valid) {
//                 // Ready for next RX
//                 CHEP(ch) = CHEP_MASK(chep) | USB_CHEP_DTOG_TX_Msk;
//                 // log_push(LogUSB_CHEP_RX, chep);
//                 // log_push(LogUSB_CHEP_LEN, len);
//             }

//         } else {
//             // Simple endpoints
//             uint32_t bd = usb_sram->chep[ch].RXTXBD;
//             uint32_t *data = TXRXDB_PTR(bd);
//             uint16_t len = TXRXBD_COUNT(bd);
//             bool rx_valid = false;
//             switch (ch) {
//             case UsbEpCDCData:
//                 rx_valid = usb_cdc_data_out(data, len);
//                 break;
//             default:
//                 DBG_BKPT("Unknown endpoint");
//             }
//             if (rx_valid) {
//                 // Ready for next RX
//                 CHEP(ch) = CHEP_MASK(chep) | CHEP_RX_VALID(chep);
//                 // log_push(LogUSB_CHEP_RX, chep);
//                 // log_push(LogUSB_CHEP_LEN, len);
//             }
//         }
//         break;
//     }

//     case EventIn:
//         if (txrx_req[ch].skip) {
//             txrx_req[ch].skip -= 1;
//         } else {
//             switch (ch) {
//             case UsbEp0Ctrl:
//                 break;
//             case UsbEpBtHciEvents:
//                 bt_hci_usb_event_confirm();
//                 break;
//             case UsbEpBtACLDataIn:
//                 bt_hci_usb_acl_confirm();
//                 break;
//             case UsbEpCDCData:
//                 usb_cdc_data_in();
//                 break;
//             default:
//                 DBG_BKPT("Unknown endpoint");
//             }
//         }
//         break;
//     }

//     usb_hw_event.rptr = (rptr + 1) % EVENT_QUEUE_SIZE;
// }
