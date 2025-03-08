#include "usb.h"
#include "semihosting.h"

#define DESC_TYPE_DEVICE                        1
#define DESC_TYPE_CONFIGURATION                 2
#define DESC_TYPE_STRING                        3
#define DESC_TYPE_INTERFACE                     4
#define DESC_TYPE_ENDPOINT                      5
#define DESC_TYPE_DEVICE_QUALIFIER              6
#define DESC_TYPE_OTHER_SPEED_CONFIGURATION     7
#define DESC_TYPE_INTERFACE_POWER               8

static const struct __attribute__((packed)) {
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
} desc_device = {
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
    .iManufacturer = 0,
    .iProduct = 0,
    .iSerialNumber = 0,
    .bNumConfigurations = 1,
};

const uint8_t *usb_desc_get(uint8_t type, uint8_t index, uint16_t *len)
{
    switch (type) {
    case DESC_TYPE_DEVICE:
        *len = sizeof(desc_device);
        return (const uint8_t *)&desc_device;
    default:
        DBG_BKPT("Unknown descriptor type");
    }
}
