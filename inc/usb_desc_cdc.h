#pragma once

#include "usb.h"
#include "macros.h"

// Table 12: Type Values for the bDescriptorType Field
#define DESC_TYPE_CS_INTERFACE  0x24
#define DESC_TYPE_CS_ENDPOINT   0x25

// CDC 5.2.3.1 Header Functional Descriptor
typedef struct PACKED {
    uint8_t bFunctionLength;
    uint8_t bDescriptorType;
    uint8_t bDescriptorSubtype;
    uint16_t bcdCDC;
} desc_cdc_header_t;

// CDC 5.2.3.2 Union Functional Descriptor
typedef struct PACKED {
    uint8_t bFunctionLength;
    uint8_t bDescriptorType;
    uint8_t bDescriptorSubtype;
    uint8_t bControlInterface;
    uint8_t bSubordinateInterface0;
    // uint8_t bSubordinateInterface[];
} desc_cdc_union_t;

// PSTN 5.3.1 Call Management Functional Descriptor
typedef struct PACKED {
    uint8_t bFunctionLength;
    uint8_t bDescriptorType;
    uint8_t bDescriptorSubtype;
    uint8_t bmCapabilities;
    uint8_t bDataInterface;
} desc_cdc_call_management_t;

// PSTN 5.3.2 Abstract Control Management Functional Descriptor
typedef struct PCKED {
    uint8_t bFunctionLength;
    uint8_t bDescriptorType;
    uint8_t bDescriptorSubtype;
    uint8_t bmCapabilities;
} desc_cdc_abstract_control_management_t;

// Class-specific descriptor
typedef struct PACKED {
    desc_cdc_header_t header;
    desc_cdc_call_management_t call;
    desc_cdc_abstract_control_management_t acm;
    desc_cdc_union_t iunion;
} desc_cdc_t;

static const desc_cdc_t desc_cdc_class = {
    .header = {
        .bFunctionLength = sizeof(desc_cdc_header_t),
        .bDescriptorType = DESC_TYPE_CS_INTERFACE,
        .bDescriptorSubtype = 0x00,     // Header Functional Descriptor
        .bcdCDC = 0x0120,
    },
    .call = {
        .bFunctionLength = sizeof(desc_cdc_call_management_t),
        .bDescriptorType = DESC_TYPE_CS_INTERFACE,
        .bDescriptorSubtype = 0x01,     // Call Management Functional Descriptor
        .bmCapabilities = 0,
        .bDataInterface = UsbInterfaceCDCData,
    },
    .acm = {
        .bFunctionLength = sizeof(desc_cdc_abstract_control_management_t),
        .bDescriptorType = DESC_TYPE_CS_INTERFACE,
        .bDescriptorSubtype = 0x02,     // Abstract Control Management Functional Descriptor
        .bmCapabilities = 0x06,         // Send_Break, Line_Coding, Serial_State
    },
    .iunion = {
        .bFunctionLength = sizeof(desc_cdc_union_t),
        .bDescriptorType = DESC_TYPE_CS_INTERFACE,
        .bDescriptorSubtype = 0x06,     // Union Functional Descriptor
        .bControlInterface = UsbInterfaceCDCComm,
        .bSubordinateInterface0 = UsbInterfaceCDCData,
    },
};
