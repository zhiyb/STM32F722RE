#include "stm32f7xx.h"
#include "semihosting.h"
#include "log.h"
#include "usb.h"

#define HW_DEV(base)    ((USB_OTG_DeviceTypeDef *)((void *)(base) + USB_OTG_DEVICE_BASE));

typedef struct usb_hw_t {
    USB_OTG_GlobalTypeDef * const base;
    const bool is_hs;
} usb_hw_t;

static usb_hw_t usb_hw[2] = {
    {.base = USB_OTG_FS, .is_hs = false},
    {.base = USB_OTG_HS, .is_hs = true},
};

#define USB_HW(hw_g)    (&usb_hw[(hw_g) == usb_hw[1].base])

static void usb_reset(usb_hw_t *hw)
{
    USB_OTG_DeviceTypeDef *hw_dev = HW_DEV(hw->base);
    hw_dev->DCFG &= ~USB_OTG_DCFG_DAD_Msk;
    // usb_hw_set_address(0);
    // usb_hw_ep_init();
    // usb_cdc_init();
    // usb_reset_handler();
}

void usb_init(USB_OTG_GlobalTypeDef *hw_g)
{
    usb_hw_t *hw = USB_HW(hw_g);
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
	hw_g->GLPMCFG = USB_OTG_GLPMCFG_ENBESL_Msk /*| USB_OTG_GLPMCFG_LPMEN_Msk*/ |
			USB_OTG_GLPMCFG_L1DSEN_Msk | USB_OTG_GLPMCFG_L1SSEN_Msk;
    if (hw->is_hs) {
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
    usb_connect(hw_g, false);

    // Initialise USB in device mode
    // Interrupts: correct transfer, reset
    usb_reset(hw);

    if (hw->is_hs) {
        // Allocate 25% for iso IN DMA, enable transceiver delay, enumerate HS,
        // ignore zero-length status OUT packets
        hw_dev->DCFG = (0b00ul << USB_OTG_DCFG_PERSCHIVL_Pos) | (1ul << 14 /* XCVRDLY */) |
                (1ul << USB_OTG_DCFG_NZLSOHSK_Msk);
    } else {
        // Allocate 25% for iso IN DMA, enumerate FS,
        // ignore zero-length status OUT packets
        hw_dev->DCFG = (0b00ul << USB_OTG_DCFG_PERSCHIVL_Pos) | (0b11ul << USB_OTG_DCFG_DSPD_Pos) |
                (1ul << USB_OTG_DCFG_NZLSOHSK_Msk);
    }

    // USB_DRD_FS->CNTR = USB_CNTR_CTRM_Msk | USB_CNTR_RESETM_Msk;
    // NVIC_EnableIRQ(USB_DRD_FS_IRQn);
}

void usb_connect(USB_OTG_GlobalTypeDef *hw_g, bool enable)
{
    usb_hw_t *hw = USB_HW(hw_g);
    USB_OTG_DeviceTypeDef *hw_dev = HW_DEV(hw->base);
    hw_dev->DCTL = enable ? 0 : USB_OTG_DCTL_SDIS_Msk;
}

// bool usb_is_connected()
// {
//     return !!(USB_DRD_FS->BCDR & USB_BCDR_DPPU_Msk);
// }

// void USB_DRD_FS_IRQHandler()
// {
//     uint32_t istr = USB_DRD_FS->ISTR;
//     uint32_t clr = 0x00037f80;  // Avoid clear unhandled interrupts
//     // log_push(LogUSB_Interrupt, istr);

//     if (istr & USB_ISTR_SOF_Msk) {
//         // Start of frame
//         log_push(LogUSB_SOF, USB_DRD_FS->FNR);
//         clr &= ~USB_ISTR_SOF_Msk;
//     }

//     if (istr & USB_ISTR_CTR_Msk) {
//         // Transfer complete
//         usb_hw_ep_ctr_irq(istr & 0x1f);
//     }

//     if (istr & USB_ISTR_RESET_Msk) {
//         // USB reset
//         usb_reset();
//         clr &= ~USB_ISTR_RESET_Msk;
//     }

//     // Clear handled interrupts
//     USB_DRD_FS->ISTR = clr;
// }

// void usb_process()
// {
//     usb_hw_ep_process();
// }
