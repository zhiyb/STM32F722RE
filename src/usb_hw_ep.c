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
    if (usb->hw.fifo_top > usb_hw_ifs[usb_if].ram_size / 4)
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
    usb_hw_ep_fifo_alloc(usb_if, usb_if == UsbIfHs ? 1024 : 512);

    hw_dev->DAINTMSK = 0;
    uint32_t daintmsk = 0;

    // IN endpoint 0
    uint32_t fifo = usb_hw_ep_fifo_alloc(usb_if, 64 * 2);
    USB_OTG_INEndpointTypeDef *hw_ep_in = HW_EP_IN(hw->base, 0);
    uint32_t epsize = usb_if == UsbIfHs ? 64 : 0;
    usb->ep[0].in.max_size = 64;
    // uint32_t epdis = hw_ep_in->DIEPCTL & USB_OTG_DIEPCTL_EPENA_Msk ? USB_OTG_DIEPCTL_EPDIS_Msk : 0;
    hw_ep_in->DIEPCTL = (fifo << USB_OTG_DIEPCTL_TXFNUM_Pos) | (epsize << USB_OTG_DIEPCTL_MPSIZ_Pos);
    daintmsk |= 1 << (USB_OTG_DAINTMSK_IEPM_Pos + 0);

    // OUT endpoint 0
    usb->ep[0].out.max_size = 64;
#if 1
    // We cannot disable/reset EP0, but reinit anyway
    usb_ep0_init(usb_if);
#else
    USB_OTG_OUTEndpointTypeDef *hw_ep_out = HW_EP_OUT(hw->base, 0);
    // epdis = hw_ep_out->DOEPCTL & USB_OTG_DOEPCTL_EPENA_Msk ? USB_OTG_DOEPCTL_EPDIS_Msk : 0;
    // If EP0 is already enabled, we cannot disable it, so cannot re-init
    if (!(hw_ep_out->DOEPCTL & USB_OTG_DOEPCTL_EPENA_Msk))
        usb_ep0_init(usb_if);
#endif
    daintmsk |= 1 << (USB_OTG_DAINTMSK_OEPM_Pos + 0);

    // Interrupt and event masks
    hw_dev->DAINTMSK = daintmsk;
    // OUT: Transfer complete, setup done, status phase
    hw_dev->DOEPMSK = USB_OTG_DOEPMSK_XFRCM_Msk |
        USB_OTG_DOEPMSK_STUPM_Msk | USB_OTG_DOEPMSK_OTEPSPRM_Msk /* STSPHSRXM */;
    // IN: Transfer complete, timeout
    hw_dev->DIEPMSK = USB_OTG_DIEPMSK_XFRCM_Msk | USB_OTG_DIEPMSK_TOM_Msk;
}

void usb_hw_ep_out(usb_if_t usb_if, uint32_t ep, void *data, uint32_t setup, uint32_t pkt, uint32_t len)
{
    usb_t *usb = &usb_ifs[usb_if];
    uint32_t max_len = usb->ep[ep].out.max_size;
    if (usb->ep[ep].out.pkts != 0)
        PANIC("EP not idle");

    if (usb_hw_ifs[usb_if].use_dma) {
        // DMA will write, so invalidate cache
        SCB_InvalidateDCache_by_Addr(data, len);
    }

    usb->ep[ep].out.p = data;
    usb->ep[ep].out.offset = 0;
    usb->ep[ep].out.len = len;
    usb->ep[ep].out.pkts = (len + max_len - 1) / max_len;
    log_push(LogUSB_Out, len);
    usb_hw_ep_out_continue(usb_if, ep, setup, pkt);
}

bool usb_hw_ep_out_continue(usb_if_t usb_if, uint32_t ep, uint32_t setup, uint32_t pkt)
{
    const usb_hw_info_t *hw = &usb_hw_ifs[usb_if];
    USB_OTG_OUTEndpointTypeDef *hw_ep_out = HW_EP_OUT(hw->base, ep);
    // Endpoint 0 does not seem to clear EPENA after OUT transfer completion?
    if (ep != 0 && (hw_ep_out->DOEPCTL & USB_OTG_DOEPCTL_EPENA_Msk))
        PANIC("EP not idle");

    usb_t *usb = &usb_ifs[usb_if];
    if (!setup && !usb->ep[ep].out.pkts)
        return false;
    if (pkt) {
        pkt = MIN(pkt, usb->ep[ep].out.pkts);
        usb->ep[ep].out.pkts -= pkt;
    }

    uint32_t max_len = usb->ep[ep].out.max_size;
    uint32_t pkt_len = usb->ep[ep].out.len - usb->ep[ep].out.offset;
    pkt_len = pkt_len >= max_len ? max_len : pkt_len;
    log_push(LogUSB_OutContinue, pkt_len);

    if (usb_hw_ifs[usb_if].use_dma) {
        hw_ep_out->DOEPDMA = (uint32_t)usb->ep[ep].out.p + usb->ep[ep].out.offset;
        usb->ep[ep].out.offset += pkt_len;
    }

    hw_ep_out->DOEPTSIZ = (setup << USB_OTG_DOEPTSIZ_STUPCNT_Pos) | (pkt << USB_OTG_DOEPTSIZ_PKTCNT_Pos) |
        (pkt_len << USB_OTG_DOEPTSIZ_XFRSIZ_Pos);
    hw_ep_out->DOEPCTL = (hw_ep_out->DOEPCTL & (USB_OTG_DOEPCTL_MPSIZ_Msk | USB_OTG_DOEPCTL_EPTYP_Msk)) |
        USB_OTG_DOEPCTL_USBAEP_Msk | USB_OTG_DOEPCTL_EPENA_Msk | USB_OTG_DOEPCTL_CNAK_Msk;
    return true;
}

void usb_hw_ep_in(usb_if_t usb_if, uint8_t ep, const void *data, uint32_t len, bool short_data)
{
    usb_t *usb = &usb_ifs[usb_if];
    uint32_t max_len = usb->ep[ep].in.max_size;
    if (usb->ep[ep].in.pkts != 0)
        PANIC("EP not idle");

    if (usb_hw_ifs[usb_if].use_dma) {
        // DMA will read, so flush cache
        SCB_CleanDCache_by_Addr(data, len);
    }

    usb->ep[ep].in.p = data;
    usb->ep[ep].in.len = len;
    usb->ep[ep].in.pkts = (len + max_len - (short_data ? 0 : 1)) / max_len;
    log_push(LogUSB_In, len);
    usb_hw_ep_in_continue(usb_if, ep);
}

bool usb_hw_ep_in_continue(usb_if_t usb_if, uint8_t ep)
{
    const usb_hw_info_t *hw = &usb_hw_ifs[usb_if];
    USB_OTG_INEndpointTypeDef *hw_ep_in = HW_EP_IN(hw->base, ep);
    if (hw_ep_in->DIEPCTL & USB_OTG_DIEPCTL_EPENA_Msk)
        PANIC("EP not idle");

    usb_t *usb = &usb_ifs[usb_if];
    if (!usb->ep[ep].in.pkts)
        return false;
    usb->ep[ep].in.pkts -= 1;

    uint32_t max_len = usb->ep[ep].in.max_size;
    uint32_t pkt_len = usb->ep[ep].in.len;
    pkt_len = pkt_len >= max_len ? max_len : pkt_len;
    usb->ep[ep].in.len -= pkt_len;
    log_push(LogUSB_InContinue, pkt_len);

    if (usb_hw_ifs[usb_if].use_dma) {
        hw_ep_in->DIEPDMA = (uint32_t)usb->ep[ep].in.p;
        usb->ep[ep].in.p += pkt_len;
    }

    hw_ep_in->DIEPTSIZ = (1 << USB_OTG_DIEPTSIZ_PKTCNT_Pos) | (pkt_len << USB_OTG_DIEPTSIZ_XFRSIZ_Pos);
    hw_ep_in->DIEPCTL = ((hw_ep_in->DIEPCTL) & (USB_OTG_DIEPCTL_TXFNUM_Msk | USB_OTG_DIEPCTL_MPSIZ_Msk |
        USB_OTG_DIEPCTL_EPTYP_Msk)) | USB_OTG_DIEPCTL_CNAK_Msk |
        USB_OTG_DIEPCTL_EPENA_Msk | USB_OTG_DIEPCTL_USBAEP_Msk;

    if (!usb_hw_ifs[usb_if].use_dma) {
        uint32_t *p = usb->ep[ep].in.p;
        usb->ep[ep].in.p += pkt_len;
        for (uint32_t i = 0; i < (pkt_len + 3) / 4; i++)
            *HW_EP_FIFO(hw->base, ep) = *p++;
    }
    return true;
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
