#include "stm32f7xx.h"
#include "semihosting.h"
#include "macros.h"
#include "usb.h"
#include "usb_dfu.h"
#include "usb_desc_hid.h"
#include "usb_desc_cdc.h"

typedef enum {
    DESC_TYPE_DEVICE                    = 1,
    DESC_TYPE_CONFIGURATION             = 2,
    DESC_TYPE_STRING                    = 3,
    DESC_TYPE_INTERFACE                 = 4,
    DESC_TYPE_ENDPOINT                  = 5,
    DESC_TYPE_DEVICE_QUALIFIER          = 6,
    DESC_TYPE_OTHER_SPEED_CONFIGURATION = 7,
    DESC_TYPE_INTERFACE_POWER           = 8,
    DESC_TYPE_OTG                       = 9,
    DESC_TYPE_DEBUG                     = 10,
    DESC_TYPE_INTERFACE_ASSOCIATION     = 11,
    DESC_TYPE_DFU_FUNCTIONAL            = 0x21,
} bDescriptorType_t;

typedef struct PACKED {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t bcdUSB;
    uint8_t bDeviceClass;
    uint8_t bDeviceSubClass;
    uint8_t bDeviceProtocol;
    uint8_t bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t iManufacturer;
    uint8_t iProduct;
    uint8_t iSerialNumber;
    uint8_t bNumConfigurations;
} desc_device_t;

typedef struct PACKED {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t wTotalLength;
    uint8_t bNumInterfaces;
    uint8_t bConfigurationValue;
    uint8_t iConfiguration;
    uint8_t bmAttributes;
    uint8_t bMaxPower;
} desc_configuration_t;

typedef struct PACKED {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bFirstInterface;
    uint8_t bInterfaceCount;
    uint8_t bFunctionClass;
    uint8_t bFunctionSubClass;
    uint8_t bFunctionProtocol;
    uint8_t iFunction;
} desc_interface_association_t;

typedef struct PACKED {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bInterfaceNumber;
    uint8_t bAlternateSetting;
    uint8_t bNumEndpoints;
    uint8_t bInterfaceClass;
    uint8_t bInterfaceSubClass;
    uint8_t bInterfaceProtocol;
    uint8_t iInterface;
} desc_interface_t;

typedef struct PACKED {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bmAttributes;
    uint16_t wDetachTimeOut;
    uint16_t wTransferSize;
    uint16_t bcdDFUVersion;
} desc_dfu_rt_functional_t;

typedef struct PACKED {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bEndpointAddress;
    uint8_t bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t bInterval;
} desc_endpoint_t;

// String descriptors
typedef struct PACKED {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t buf[];
} desc_string_lang_t;

// static desc_string_lang_t desc_string_buf ALIGNED(4);

static void desc_string_copy(desc_string_lang_t *desc, const void *data, uint16_t len)
{
    // Convert from ASCII to UTF-16 encoding
    desc->bLength = 2 + len * 2;
    desc->bDescriptorType = DESC_TYPE_STRING;
    const uint8_t *src = data;
    for (uint16_t i = 0; i < len; i++) {
        desc->buf[i * 2 + 0] = src[i];
        desc->buf[i * 2 + 1] = 0;
    }
}

typedef enum {
    String_LANG = 0,
    String_iManufacturer,
    String_iProduct,
    String_iSerialNumber,
    String_CDC,
    String_DFU_RT,
    String_DFU_Mode,
    NumStrings,
} desc_string_index_t;

static const uint8_t *desc_string(uint8_t *desc_buf, uint8_t index, uint16_t *len)
{
    desc_string_lang_t *desc = (desc_string_lang_t *)desc_buf;
    switch ((desc_string_index_t)index) {
    case String_LANG: {
        static const uint16_t wLANGID[] = {
            0x0809,     // English (United Kingdom)
        };
        desc->bLength = 2 + sizeof(wLANGID);
        desc->bDescriptorType = DESC_TYPE_STRING;
        uint16_t *dst = (uint16_t *)&desc->buf[0];
        for (uint16_t i = 0; i < ARRAY_SIZE(wLANGID); i++)
            dst[i] = wLANGID[i];
        break;
    }
    case String_iManufacturer: {
        static const uint8_t str[] = "STMicroelectronics";
        desc_string_copy(desc, str, sizeof(str) - 1);
        break;
    }
    case String_iProduct: {
        static const uint8_t str[] = "STM32F722RE";
        desc_string_copy(desc, str, sizeof(str) - 1);
        break;
    }
    case String_iSerialNumber: {
        uint8_t str[96 / 4 + 1];
        uint32_t *uid = (uint32_t *)UID_BASE;
        for (uint32_t i = 0; i < 96 / 4; i++) {
            uint8_t v = ((uid[i / 8] >> ((i % 8) / 2)) >> (i % 2 ? 4 : 0)) & 0x0f;
            str[i] = v >= 10 ? v - 10 + 'a' : v + '0';
        }
        str[96 / 4] = '\0';
        // static const uint8_t str[] = "(String_iSerialNumber)";
        desc_string_copy(desc, str, sizeof(str) - 1);
        break;
    }
    case String_CDC: {
        static const uint8_t str[] = "(String_CDC)";
        desc_string_copy(desc, str, sizeof(str) - 1);
        break;
    }
    case String_DFU_RT: {
        static const uint8_t str[] = "(String_DFU_RT)";
        desc_string_copy(desc, str, sizeof(str) - 1);
        break;
    }
    case String_DFU_Mode: {
        static const uint8_t str[] = "(String_DFU_Mode)";
        desc_string_copy(desc, str, sizeof(str) - 1);
        break;
    }
    default:
        // dbg_puts("Unknown string descriptor");
        return 0;
    }

    *len = desc->bLength;
    return (const uint8_t *)desc;
}

// Descriptor data

static const desc_device_t desc_device ALIGNED(4) = {
    .bLength = sizeof(desc_device_t),
    .bDescriptorType = DESC_TYPE_DEVICE,
    .bcdUSB = 0x0200,
#ifndef BOOTLOADER
#if USB_ALT_IF != USB_ALT_IF_NONE
    .bDeviceClass = 0xef,       // Miscellaneous Device
    .bDeviceSubClass = 2,
    .bDeviceProtocol = 1,       // Interface Association
#else
    .bDeviceClass = 0xe0,       // Wireless Controller
    .bDeviceSubClass = 0x01,    // RF Controller
    .bDeviceProtocol = 0x01,    // Bluetooth Primary Controller
#endif
#else
    .bDeviceClass = 0xfe,       // Application Specific
    .bDeviceSubClass = 0x01,    // Device Firmware Upgrade
    .bDeviceProtocol = 0x01,    // Runtime
#endif
    .bMaxPacketSize0 = 64,
    .idVendor = 0x0483,         // STMicroelectronics
    .idProduct = 0x5750,
    .bcdDevice = 0,
    .iManufacturer = String_iManufacturer,
    .iProduct = String_iProduct,
    .iSerialNumber = String_iSerialNumber,
    .bNumConfigurations = 1,
};

static const struct PACKED {
    desc_configuration_t configuration;

#ifndef BOOTLOADER
#if USB_ALT_IF == USB_ALT_IF_HID
    struct PACKED {
        desc_interface_t interface;
        desc_hid_t hid;
        desc_endpoint_t endpoint;
    } hid;

#elif USB_ALT_IF == USB_ALT_IF_CDC
    struct PACKED {
        desc_interface_association_t iassoc;
        struct PACKED {
            desc_interface_t interface;
            desc_cdc_t cdc;
            desc_endpoint_t endpoint;
        } comm;
        struct PACKED {
            desc_interface_t interface;
            desc_endpoint_t endpoint_out;
            desc_endpoint_t endpoint_in;
        } data;
    } cdc;
#endif

#else   // BOOTLOADER
    struct PACKED {
        desc_interface_t interface;
        desc_dfu_rt_functional_t functional;
    } dfu_rt;
#endif

} desc_configuration ALIGNED(4) = {
    .configuration = {
        .bLength = sizeof(desc_configuration_t),
        .bDescriptorType = DESC_TYPE_CONFIGURATION,
        .wTotalLength = sizeof(desc_configuration),
        .bNumInterfaces = UsbNumInterfaces,
        .bConfigurationValue = 1,
        .iConfiguration = 0,
        .bmAttributes = 0xe0,
        .bMaxPower = 100 / 2,
    },

#ifndef BOOTLOADER
#if USB_ALT_IF == USB_ALT_IF_HID
    .hid = {
        .interface = {
            .bLength = sizeof(desc_interface_t),
            .bDescriptorType = DESC_TYPE_INTERFACE,
            .bInterfaceNumber = UsbInterfaceHid,
            .bAlternateSetting = 0,
            .bNumEndpoints = 1,
            .bInterfaceClass = 0x03,
            .bInterfaceSubClass = 0x01, // Boot interface
            .bInterfaceProtocol = 0x01, // Keyboard
            .iInterface = 0,
        },
        .hid = desc_hid,
        .endpoint = {
            .bLength = sizeof(desc_endpoint_t),
            .bDescriptorType = DESC_TYPE_ENDPOINT,
            .bEndpointAddress = 0x80 | UsbEpHid,    // IN
            .bmAttributes = 0b11,       // Interrupt
            .wMaxPacketSize = 8,
            .bInterval = 10,            // 10ms polling interval
        },
    },

#elif USB_ALT_IF == USB_ALT_IF_CDC
    .cdc = {
        .iassoc = {
            .bLength = sizeof(desc_interface_association_t),
            .bDescriptorType = DESC_TYPE_INTERFACE_ASSOCIATION,
            .bFirstInterface = UsbInterfaceCDCComm,
            .bInterfaceCount = 2,
            .bFunctionClass = 2,        // Communications
            .bFunctionSubClass = 2,     // Abstract (modem)
            .bFunctionProtocol = 1,     // AT-commands (v.25ter)
            .iFunction = String_CDC,
        },
        .comm = {
            .interface = {
                .bLength = sizeof(desc_interface_t),
                .bDescriptorType = DESC_TYPE_INTERFACE,
                .bInterfaceNumber = UsbInterfaceCDCComm,
                .bAlternateSetting = 0,
                .bNumEndpoints = 1,
                .bInterfaceClass = 2,       // Communications
                .bInterfaceSubClass = 2,    // Abstract (modem)
                .bInterfaceProtocol = 1,    // AT-commands (v.25ter)
                .iInterface = String_CDC,
            },
            .cdc = desc_cdc_class,
            .endpoint = {
                .bLength = sizeof(desc_endpoint_t),
                .bDescriptorType = DESC_TYPE_ENDPOINT,
                .bEndpointAddress = 0x80 | UsbEpCDCComm,    // IN
                .bmAttributes = 0b11,       // Interrupt
                .wMaxPacketSize = 8,
                .bInterval = 16,            // 16ms polling interval
            },
        },
        .data = {
            .interface = {
                .bLength = sizeof(desc_interface_t),
                .bDescriptorType = DESC_TYPE_INTERFACE,
                .bInterfaceNumber = UsbInterfaceCDCData,
                .bAlternateSetting = 0,
                .bNumEndpoints = 2,
                .bInterfaceClass = 10,      // CDC Data
                .bInterfaceSubClass = 0,
                .bInterfaceProtocol = 0,
                .iInterface = String_CDC,
            },
            .endpoint_out = {
                .bLength = sizeof(desc_endpoint_t),
                .bDescriptorType = DESC_TYPE_ENDPOINT,
                .bEndpointAddress = 0x00 | UsbEpCDCData,    // OUT
                .bmAttributes = 0b10,       // Bulk
                .wMaxPacketSize = 64,
                .bInterval = 0,
            },
            .endpoint_in = {
                .bLength = sizeof(desc_endpoint_t),
                .bDescriptorType = DESC_TYPE_ENDPOINT,
                .bEndpointAddress = 0x80 | UsbEpCDCData,    // IN
                .bmAttributes = 0b10,       // Bulk
                .wMaxPacketSize = 64,
                .bInterval = 0,
            },
        },
    },
#endif

#else   // BOOTLOADER
    .dfu_rt = {
        .interface = {
            .bLength = sizeof(desc_interface_t),
            .bDescriptorType = DESC_TYPE_INTERFACE,
            .bInterfaceNumber = UsbInterfaceDfuRT,
            .bAlternateSetting = 0,
            .bNumEndpoints = 0,
            .bInterfaceClass = 0xfe,    // Application Specific
            .bInterfaceSubClass = 1,    // Device Firmware Upgrade
            .bInterfaceProtocol = 1,    // Runtime
            .iInterface = String_DFU_RT,
        },
        .functional = {
            .bLength = sizeof(desc_dfu_rt_functional_t),
            .bDescriptorType = DESC_TYPE_DFU_FUNCTIONAL,
            // bitWillDetach, bitManifestationTolerant
            // bitCanUpload, bitCanDnload
            .bmAttributes = 0x0f,
            .wDetachTimeOut = 1000,
            .wTransferSize = USB_DFU_TRANSFER_SIZE,
            .bcdDFUVersion = 0x0101,
        },
    },
#endif
};

static const desc_device_t desc_dfu_device ALIGNED(4) = {
    .bLength = sizeof(desc_device_t),
    .bDescriptorType = DESC_TYPE_DEVICE,
    .bcdUSB = 0x0100,
    .bDeviceClass = 0x00,       // See interface
    .bDeviceSubClass = 0x00,    // See interface
    .bDeviceProtocol = 0x00,    // See interface
    .bMaxPacketSize0 = 64,
    .idVendor = 0x0483,         // STMicroelectronics
    .idProduct = 0x5750,
    .bcdDevice = 0,
    .iManufacturer = String_iManufacturer,
    .iProduct = String_iProduct,
    .iSerialNumber = String_iSerialNumber,
    .bNumConfigurations = 1,
};

static const struct PACKED {
    desc_configuration_t configuration;
    struct PACKED {
        desc_interface_t interface;
        desc_dfu_rt_functional_t functional;
    } dfu_mode;
} desc_dfu_configuration ALIGNED(4) = {
    .configuration = {
        .bLength = sizeof(desc_configuration_t),
        .bDescriptorType = DESC_TYPE_CONFIGURATION,
        .wTotalLength = sizeof(desc_dfu_configuration),
        .bNumInterfaces = 1,
        .bConfigurationValue = 1,
        .iConfiguration = 0,
        .bmAttributes = 0xe0,
        .bMaxPower = 100 / 2,
    },
    .dfu_mode = {
        .interface = {
            .bLength = sizeof(desc_interface_t),
            .bDescriptorType = DESC_TYPE_INTERFACE,
            .bInterfaceNumber = UsbInterfaceDfuMode,
            .bAlternateSetting = 0,
            .bNumEndpoints = 0,
            .bInterfaceClass = 0xfe,    // Application Specific
            .bInterfaceSubClass = 0x01, // Device Firmware Upgrade
            .bInterfaceProtocol = 0x02, // DFU mode
            .iInterface = String_DFU_Mode,
        },
        .functional = {
            .bLength = sizeof(desc_dfu_rt_functional_t),
            .bDescriptorType = DESC_TYPE_DFU_FUNCTIONAL,
            // bitWillDetach, bitManifestationTolerant
            // bitCanUpload, bitCanDnload
            .bmAttributes = 0x0f,
            .wDetachTimeOut = 1000,
            .wTransferSize = USB_DFU_TRANSFER_SIZE,
            .bcdDFUVersion = 0x0101,
        },
    },
};

const uint8_t *usb_desc_get(uint8_t *desc_buf, uint8_t type, uint8_t index, uint16_t *len)
{
    switch (type) {
    case DESC_TYPE_DEVICE:
#ifdef BOOTLOADER
        if (usb_dfu_state() >= UsbDfuState_dfuIDLE) {
            *len = sizeof(desc_dfu_device);
            return (const uint8_t *)&desc_dfu_device;
        } else {
            *len = sizeof(desc_device);
            return (const uint8_t *)&desc_device;
        }
#else
        *len = sizeof(desc_device);
        return (const uint8_t *)&desc_device;
#endif
    case DESC_TYPE_CONFIGURATION:
#ifdef BOOTLOADER
        if (usb_dfu_state() >= UsbDfuState_dfuIDLE) {
            *len = sizeof(desc_dfu_configuration);
            return (const uint8_t *)&desc_dfu_configuration;
        } else {
            *len = sizeof(desc_configuration);
            return (const uint8_t *)&desc_configuration;
        }
#else
        *len = sizeof(desc_configuration);
        return (const uint8_t *)&desc_configuration;
#endif
    case DESC_TYPE_DEVICE_QUALIFIER:
        // Not a high speed device, not supported
        return 0;
    case DESC_TYPE_STRING:
        return desc_string(desc_buf, index, len);
    case DESC_TYPE_OTG:
    case DESC_TYPE_DEBUG:
    case DESC_TYPE_INTERFACE_ASSOCIATION:
        // Not supported
        return 0;
    default:
        DBG_BKPT("Unknown descriptor type");
        return 0;
    }
}
