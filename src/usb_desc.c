#include "usb.h"
#include "usb_desc_hid.h"
#include "semihosting.h"

#define DESC_TYPE_DEVICE                        1
#define DESC_TYPE_CONFIGURATION                 2
#define DESC_TYPE_STRING                        3
#define DESC_TYPE_INTERFACE                     4
#define DESC_TYPE_ENDPOINT                      5
#define DESC_TYPE_DEVICE_QUALIFIER              6
#define DESC_TYPE_OTHER_SPEED_CONFIGURATION     7
#define DESC_TYPE_INTERFACE_POWER               8

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
    uint8_t bEndpointAddress;
    uint8_t bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t bInterval;
} desc_endpoint_t;

// String descriptors
typedef struct PACKED {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t buf[64];
} desc_string_lang_t;

static desc_string_lang_t desc_string_buf ALIGNED(4);

static void desc_string_copy(const void *data, uint16_t len)
{
    // Convert from ASCII to UTF-16 encoding
    desc_string_buf.bLength = 2 + len * 2;
    desc_string_buf.bDescriptorType = DESC_TYPE_STRING;
    const uint8_t *src = data;
    for (uint16_t i = 0; i < len; i++) {
        desc_string_buf.buf[i * 2 + 0] = src[i];
        desc_string_buf.buf[i * 2 + 1] = 0;
    }
}

typedef enum {
    String_LANG = 0,
    String_iManufacturer,
    String_iProduct,
    String_iSerialNumber,
    NumStrings,
} desc_string_index_t;

static const uint8_t *desc_string(uint8_t index, uint16_t *len)
{
    switch ((desc_string_index_t)index) {
    case String_LANG: {
        static const uint16_t wLANGID[] = {
            0x0809,     // English (United Kingdom)
        };
        desc_string_buf.bLength = 2 + sizeof(wLANGID);
        desc_string_buf.bDescriptorType = DESC_TYPE_STRING;
        uint16_t *dst = (uint16_t *)&desc_string_buf.buf[0];
        for (uint16_t i = 0; i < sizeof(wLANGID); i++)
            dst[i] = wLANGID[i];
        break;
    }
    case String_iManufacturer: {
        static const uint8_t str[] = "STMicroelectronics";
        desc_string_copy(str, sizeof(str) - 1);
        break;
    }
    case String_iProduct: {
        static const uint8_t str[] = "STM32C071RB";
        desc_string_copy(str, sizeof(str) - 1);
        break;
    }
    case String_iSerialNumber: {
        static const uint8_t str[] = "(String_iSerialNumber)";
        desc_string_copy(str, sizeof(str) - 1);
        break;
    }
    default:
        // dbg_puts("Unknown string descriptor");
        return (void *)-1;
    }

    *len = desc_string_buf.bLength;
    return (const uint8_t *)&desc_string_buf;
}

// Descriptor data

static const desc_device_t desc_device ALIGNED(4) = {
    .bLength = sizeof(desc_device),
    .bDescriptorType = DESC_TYPE_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0,      // Interface Class Defined Device
    .bDeviceSubClass = 0,
    .bDeviceProtocol = 0,
    .bMaxPacketSize0 = 64,
    .idVendor = 0x0483,     // STMicroelectronics
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
        desc_hid_t hid;
        desc_endpoint_t endpoint;
    } hid;

} desc_configuration ALIGNED(4) = {
    .configuration = {
        .bLength = sizeof(desc_configuration_t),
        .bDescriptorType = DESC_TYPE_CONFIGURATION,
        .wTotalLength = sizeof(desc_configuration),
        .bNumInterfaces = 1,
        .bConfigurationValue = 1,
        .iConfiguration = 0,
        .bmAttributes = 0xe0,

    },

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
            .bEndpointAddress = 0x81,   // IN, endpoint 1
            .bmAttributes = 0b11,       // Interrupt
            .wMaxPacketSize = 8,
            .bInterval = 10,            // 10ms polling interval
        },
    },
};

const uint8_t *usb_desc_get(uint8_t type, uint8_t index, uint16_t *len)
{
    switch (type) {
    case DESC_TYPE_DEVICE:
        *len = sizeof(desc_device);
        return (const uint8_t *)&desc_device;
    case DESC_TYPE_CONFIGURATION:
        *len = sizeof(desc_configuration);
        return (const uint8_t *)&desc_configuration;
    case DESC_TYPE_DEVICE_QUALIFIER:
        // Not a high speed device, not supported
        return 0;
    case DESC_TYPE_STRING:
        return desc_string(index, len);
    case 10:
        // Debug descriptor?
        return 0;
    default:
        DBG_BKPT("Unknown descriptor type");
    }
}
