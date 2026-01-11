#pragma once

#define HW_G(base)              ((USB_OTG_GlobalTypeDef *)(base))
#define HW_DEV(base)            ((USB_OTG_DeviceTypeDef *)((base) + USB_OTG_DEVICE_BASE))
#define HW_EP_IN(base, ep)      ((USB_OTG_INEndpointTypeDef *)((base) + USB_OTG_IN_ENDPOINT_BASE + (ep) * USB_OTG_EP_REG_SIZE))
#define HW_EP_OUT(base, ep)     ((USB_OTG_OUTEndpointTypeDef *)((base) + USB_OTG_OUT_ENDPOINT_BASE + (ep) * USB_OTG_EP_REG_SIZE))
#define HW_EP_FIFO(base, ep)    ((volatile uint32_t *)((base) + USB_OTG_FIFO_BASE + (ep) * USB_OTG_FIFO_SIZE))

#define DIEPTXF(a, d)	        (((d) << USB_OTG_DIEPTXF_INEPTXFD_Pos) | ((a) << USB_OTG_DIEPTXF_INEPTXSA_Pos))
