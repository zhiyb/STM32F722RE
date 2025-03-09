#pragma once

#include "usb.h"

#define DESC_TYPE_HID           0x21
#define DESC_TYPE_HID_REPORT    0x22

// 6.2.1 HID Descriptor
typedef struct PACKED {
    uint8_t bDescriptorType;
    uint16_t wDescriptorLength;
} desc_hid_class_t;

typedef struct PACKED {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t bcdHID;
    uint8_t bCountryCode;
    uint8_t bNumDescriptors;
    desc_hid_class_t report;
    desc_hid_class_t optional[0];
} desc_hid_t;

#include "usb_desc_hid_report.h"

static const desc_hid_t desc_hid = {
    .bLength = sizeof(desc_hid),
    .bDescriptorType = DESC_TYPE_HID,
    .bcdHID = 0x0101,
    .bCountryCode = 0,
    .bNumDescriptors = 1,
    .report = {
        .bDescriptorType = DESC_TYPE_HID_REPORT,
        .wDescriptorLength = sizeof(hidReportDescriptor),
    },
};
