#include "stm32f7xx.h"
#include "semihosting.h"
#include "log.h"
#include "irq.h"
#include "usb.h"
#include "usb_hw.h"
#include "usb_internal.h"

const usb_hw_info_t usb_hw_ifs[NumUsbIfs] = {
    [UsbIfFs] = {
        .base = USB_OTG_FS_PERIPH_BASE,
        .ram_size = 1280,
        .use_dma = false,
    },
    [UsbIfHs] = {
        .base = USB_OTG_HS_PERIPH_BASE,
        .ram_size = 1024 * 4,
        .use_dma = true,
    },
};

static void usb_hw_reset(usb_if_t usb_if)
{
    // Reset device address
    const usb_hw_info_t *hw = &usb_hw_ifs[usb_if];
    USB_OTG_DeviceTypeDef *hw_dev = HW_DEV(hw->base);
    hw_dev->DCFG &= ~USB_OTG_DCFG_DAD_Msk;

    usb_t *usb = &usb_ifs[usb_if];
    usb->hw.daddr = 0;
    usb->hw.daddr_change = false;
    usb->ev.rptr = 0;
    usb->ev.wptr = 0;
    usb->ev.data[0].ev = UsbEvNone;
}

void usb_hw_init(usb_if_t usb_if)
{
    const usb_hw_info_t *hw = &usb_hw_ifs[usb_if];
    USB_OTG_GlobalTypeDef *hw_g = HW_G(hw->base);
    USB_OTG_DeviceTypeDef *hw_dev = HW_DEV(hw->base);

	// Wait for AHB bus transactions
	while (!(hw_g->GRSTCTL & USB_OTG_GRSTCTL_AHBIDL_Msk));
	// Core reset
	hw_g->GRSTCTL |= USB_OTG_GRSTCTL_CSRST_Msk;
	while (hw_g->GRSTCTL & USB_OTG_GRSTCTL_CSRST_Msk);

	// OTG version 1.3 is obsolete, select version 2.0
    // Override B-session (device) valid
    // Override A-session (host) invalid
    // Override V_BUS valid
	hw_g->GOTGCTL = USB_OTG_GOTGCTL_OTGVER_Msk |
            USB_OTG_GOTGCTL_BVALOVAL_Msk | USB_OTG_GOTGCTL_BVALOEN_Msk |
            USB_OTG_GOTGCTL_AVALOEN_Msk |
            USB_OTG_GOTGCTL_VBVALOVAL_Msk | USB_OTG_GOTGCTL_VBVALOEN_Msk;
	// Enable LPM errata behaviour, L1 deep/shallow sleep enable, LPM disable
	hw_g->GLPMCFG = USB_OTG_GLPMCFG_ENBESL_Msk | USB_OTG_GLPMCFG_LPMEN_Msk |
			USB_OTG_GLPMCFG_L1DSEN_Msk | USB_OTG_GLPMCFG_L1SSEN_Msk;
    if (usb_if == UsbIfHs) {
        // VBUS detection disabled, USB HS PHY enabled
        hw_g->GCCFG = 0;
        // Force device mode, TRDT = 9, HNP and SRP not capable, external ULPI HS PHY
        hw_g->GUSBCFG = USB_OTG_GUSBCFG_FDMOD_Msk | USB_OTG_GUSBCFG_ULPI_UTMI_SEL_Msk |
                // USB_OTG_GUSBCFG_HNPCAP_Msk | USB_OTG_GUSBCFG_SRPCAP_Msk |
                (9 << USB_OTG_GUSBCFG_TRDT_Pos) | (4 << USB_OTG_GUSBCFG_TOCAL_Pos);
    } else {
        // VBUS detection disabled, USB FS PHY enabled
        hw_g->GCCFG = USB_OTG_GCCFG_PWRDWN_Msk;
        // Force device mode, TRDT = 6, HNP and SRP not capable
        hw_g->GUSBCFG = USB_OTG_GUSBCFG_FDMOD_Msk |
                // USB_OTG_GUSBCFG_HNPCAP_Msk | USB_OTG_GUSBCFG_SRPCAP_Msk |
                (6 << USB_OTG_GUSBCFG_TRDT_Pos) | (0 << USB_OTG_GUSBCFG_TOCAL_Pos);
    }

    // Initialise in disconnected state
    usb_hw_connect(usb_if, false);

    // Initialise USB in device mode
    if (usb_if == UsbIfHs) {
        // Allocate 25% for iso IN DMA, enable transceiver delay, enumerate HS,
        // ignore zero-length status OUT packets
        hw_dev->DCFG = (0b00ul << USB_OTG_DCFG_PERSCHIVL_Pos) | (1ul << 14 /* XCVRDLY */) |
                (1ul << USB_OTG_DCFG_NZLSOHSK_Msk);
#if 0
        // Data transfer threshold
        hw_dev->DTHRCTL = (32u << USB_OTG_DTHRCTL_RXTHRLEN_Pos) | (32u << USB_OTG_DTHRCTL_TXTHRLEN_Pos) |
                USB_OTG_DTHRCTL_RXTHREN_Msk | USB_OTG_DTHRCTL_ISOTHREN_Msk |
                USB_OTG_DTHRCTL_NONISOTHREN_Msk | USB_OTG_DTHRCTL_ARPEN_Msk;
#else
        // Disable thresholding
        hw_dev->DTHRCTL = 0;
#endif
        // DMA disable
        hw_g->GAHBCFG = 0;
        // DMA enable, AHB burst 32 bytes
        // hw_g->GAHBCFG = USB_OTG_GAHBCFG_DMAEN_Msk | (5 << USB_OTG_GAHBCFG_HBSTLEN_Pos);
    } else {
        // Enumerate FS, ignore zero-length status OUT packets
        hw_dev->DCFG = (0b11ul << USB_OTG_DCFG_DSPD_Pos) |
                (1ul << USB_OTG_DCFG_NZLSOHSK_Msk);
        // No DMA
        hw_g->GAHBCFG = 0;
    }

    usb_hw_reset(usb_if);

    // Without DMA, we need to manually read the RX FIFO
    uint32_t gint = hw->use_dma ? 0 : USB_OTG_GINTSTS_RXFLVL_Msk;
    // Enable global interrupts: reset, enumeration, OUT and IN endpoints
    hw_g->GINTSTS = gint | USB_OTG_GINTSTS_USBRST_Msk | USB_OTG_GINTSTS_ENUMDNE_Msk;
	hw_g->GINTMSK = gint | USB_OTG_GINTMSK_USBRST_Msk | USB_OTG_GINTMSK_ENUMDNEM_Msk |
        USB_OTG_GINTMSK_OEPINT_Msk | USB_OTG_GINTMSK_IEPINT_Msk;
	// Unmask interrupts
	hw_g->GAHBCFG |= USB_OTG_GAHBCFG_GINT_Msk /* GINTMSK */;

	// Setup NVIC interrupts
	uint32_t pg = NVIC_GetPriorityGrouping();
    if (usb_if == UsbIfHs) {
        NVIC_SetPriority(OTG_HS_IRQn,
            NVIC_EncodePriority(pg, NvicPriorityUsbHsLP, 0));
        // NVIC_SetPriority(OTG_HS_WKUP_IRQn,
        // 		 NVIC_EncodePriority(pg, NVIC_PRIORITY_USB, 0));
        // NVIC_SetPriority(OTG_HS_EP1_IN_IRQn,
        // 		 NVIC_EncodePriority(pg, NVIC_PRIORITY_USB_H, 0));
        // NVIC_SetPriority(OTG_HS_EP1_OUT_IRQn,
        // 		 NVIC_EncodePriority(pg, NVIC_PRIORITY_USB_H, 0));
        NVIC_EnableIRQ(OTG_HS_IRQn);
        // NVIC_EnableIRQ(OTG_HS_WKUP_IRQn);
        // NVIC_EnableIRQ(OTG_HS_EP1_IN_IRQn);
        // NVIC_EnableIRQ(OTG_HS_EP1_OUT_IRQn);
    } else {
        NVIC_SetPriority(OTG_FS_IRQn,
            NVIC_EncodePriority(pg, NvicPriorityUsbFs, 0));
        NVIC_EnableIRQ(OTG_FS_IRQn);
    }
}

void usb_hw_connect(usb_if_t usb_if, bool enable)
{
    const usb_hw_info_t *hw = &usb_hw_ifs[usb_if];
    USB_OTG_DeviceTypeDef *hw_dev = HW_DEV(hw->base);
    hw_dev->DCTL = enable ? 0 : USB_OTG_DCTL_SDIS_Msk;
}

bool usb_hw_is_connected(usb_if_t usb_if)
{
    const usb_hw_info_t *hw = &usb_hw_ifs[usb_if];
    USB_OTG_DeviceTypeDef *hw_dev = HW_DEV(hw->base);
    return !(hw_dev->DCTL & USB_OTG_DCTL_SDIS_Msk);
}

void usb_hw_set_address(usb_if_t usb_if, uint16_t addr)
{
#if 1
    // STM32F7 needs register updated before Status stage
    const usb_hw_info_t *hw = &usb_hw_ifs[usb_if];
    USB_OTG_DeviceTypeDef *hw_dev = HW_DEV(hw->base);
    hw_dev->DCFG = (hw_dev->DCFG & ~USB_OTG_DCFG_DAD_Msk) | ((addr << USB_OTG_DCFG_DAD_Pos) & USB_OTG_DCFG_DAD_Msk);
#else
    usb_t *usb = &usb_ifs[usb_if];
    usb->hw.daddr = addr;
    usb->hw.daddr_change = true;
#endif
    log_push(LogUSB_SetAddress, addr);
}

static void usb_hw_push_event(usb_t *usb, uint8_t ep, usb_ev_id_t ev)
{
    uint8_t wptr = usb->ev.wptr;
    uint8_t wptr_next = (usb->ev.wptr + 1) % USB_MAX_NUM_EV;
    usb->ev.data[wptr_next].ev = UsbEvNone;
    usb->ev.data[wptr] = (usb_ev_t){.ep = ep, .ev = ev};
    usb->ev.wptr = wptr_next;
}

static void usb_hw_irq(usb_if_t usb_if)
{
    const usb_hw_info_t *hw = &usb_hw_ifs[usb_if];
    USB_OTG_GlobalTypeDef *hw_g = HW_G(hw->base);
    USB_OTG_DeviceTypeDef *hw_dev = HW_DEV(hw->base);
    uint32_t gintsts = hw_g->GINTSTS;
    usb_t *usb = &usb_ifs[usb_if];
    bool handled = false;

    log_push(LogUSB_Interface, usb_if);
    log_push(LogUSB_Interrupt, gintsts);

    // USB reset
    if (gintsts & USB_OTG_GINTSTS_USBRST_Msk) {
        hw_g->GINTSTS = USB_OTG_GINTSTS_USBRST_Msk;
        handled = true;
        usb_hw_reset(usb_if);
    }

    // Enumeration done
    if (gintsts & USB_OTG_GINTSTS_ENUMDNE_Msk) {
        handled = true;
        hw_g->GINTSTS = USB_OTG_GINTSTS_ENUMDNE_Msk;
        usb_hw_ep_init(usb_if);
    }

    // RX FIFO packet available
    if (!hw->use_dma && (gintsts & USB_OTG_GINTSTS_RXFLVL_Msk)) {
        handled = true;
        uint32_t grxstsp = hw_g->GRXSTSP;
        log_push(LogUSB_RX, grxstsp);
        uint32_t ep = (grxstsp & USB_OTG_GRXSTSP_EPNUM_Msk) >> USB_OTG_GRXSTSP_EPNUM_Pos;
        uint32_t cnt = (grxstsp & USB_OTG_GRXSTSP_BCNT_Msk) >> USB_OTG_GRXSTSP_BCNT_Pos;
        uint32_t *p = usb->ep[ep].out.p;
        cnt = (cnt + 3) / 4;
        while (cnt--) {
            // uint32_t v = *HW_EP_FIFO(hw->base, ep);
            // *p++ = v;
            // log_push(LogUSB_RX_DATA, v);
            *p++ = *HW_EP_FIFO(hw->base, ep);
        }
        usb->ep[ep].out.p = p;
    }

    // OUT endpoint interrupt
    if (gintsts & USB_OTG_GINTSTS_OEPINT_Msk) {
        uint16_t daint = hw_dev->DAINT >> USB_OTG_DAINT_OEPINT_Pos;
        log_push(LogUSB_OUT_INT, daint);
        for (uint8_t ep = 0; daint; daint >>= 1, ep += 1) {
            USB_OTG_OUTEndpointTypeDef *hw_ep_out = HW_EP_OUT(hw->base, ep);
            uint32_t ep_int = hw_ep_out->DOEPINT;
            log_push(LogUSB_OUT_EP_INT, ep_int);
            if (ep_int & USB_OTG_DOEPINT_STUP_Msk) {
                handled = true;
                hw_ep_out->DOEPINT = USB_OTG_DOEPINT_STUP_Msk;
                usb_hw_push_event(usb, ep, UsbEvSetup);
            }
            if (ep_int & USB_OTG_DOEPINT_XFRC_Msk) {
                handled = true;
                hw_ep_out->DOEPINT = USB_OTG_DOEPINT_XFRC_Msk;
            }
        }
    }

    // IN endpoint interrupt
    if (gintsts & USB_OTG_GINTSTS_IEPINT_Msk) {
        uint16_t daint = hw_dev->DAINT >> USB_OTG_DAINT_IEPINT_Pos;
        log_push(LogUSB_IN_INT, daint);
        for (uint8_t ep = 0; daint; daint >>= 1, ep += 1) {
            USB_OTG_INEndpointTypeDef *hw_ep_in = HW_EP_IN(hw->base, ep);
            uint32_t ep_int = hw_ep_in->DIEPINT;
            log_push(LogUSB_IN_EP_INT, ep_int);
            if (ep_int & USB_OTG_DIEPINT_XFRC_Msk) {
                handled = true;
                hw_ep_in->DIEPINT = USB_OTG_DIEPINT_XFRC_Msk;
                if (usb->ep[ep].in.pkts)
                    usb_hw_ep_in_continue(usb_if, ep);
                else
                    usb_hw_push_event(usb, ep, UsbEvIn);
                if (ep == 0 && usb->hw.daddr_change) {
                    hw_dev->DCFG = (hw_dev->DCFG & ~USB_OTG_DCFG_DAD_Msk) | (usb->hw.daddr << USB_OTG_DCFG_DAD_Pos);
                    usb->hw.daddr_change = false;
                    log_push(LogUSB_SetAddress_INT, usb->hw.daddr);
                }
            }
            if (ep_int & USB_OTG_DIEPINT_TOC_Msk) {
                handled = true;
                hw_ep_in->DIEPINT = USB_OTG_DIEPINT_TOC_Msk;
                usb->ep[ep].in.pkts = 0;
            }
        }
    }

    if (!handled) {
        DBG_BKPT("USB_IRQ");
    }
}

void usb_hw_process(usb_if_t usb_if)
{
    usb_t *usb = &usb_ifs[usb_if];
    uint8_t rptr = usb->ev.rptr;
    usb_ev_t ev = usb->ev.data[rptr];
    if (ev.ev == UsbEvNone)
        return;
    usb->ev.rptr = (rptr + 1) % USB_MAX_NUM_EV;

    switch ((usb_ev_id_t)ev.ev) {
    case UsbEvSetup:
        if (ev.ep != 0)
            PANIC("SETUP received on unexpected endpoint");
        usb_ep0_setup(usb_if);
        break;

    case UsbEvIn:
        break;

    default:
        TODO();
    }
}

void OTG_FS_IRQHandler()
{
    usb_hw_irq(UsbIfFs);
}

void OTG_HS_IRQHandler()
{
    usb_hw_irq(UsbIfHs);
}
