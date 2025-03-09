#include "usb.h"
#include "usb_desc_hid.h"
#include "semihosting.h"

#define REQ_CLASS_SEND_ENCAPSULATED_COMMAND                     0x00
#define REQ_CLASS_GET_ENCAPSULATED_RESPONSE                     0x01
#define REQ_CLASS_SET_COMM_FEATURE                              0x02
#define REQ_CLASS_GET_COMM_FEATURE                              0x03
#define REQ_CLASS_CLEAR_COMM_FEATURE                            0x04
#define REQ_CLASS_SET_AUX_LINE_STATE                            0x10
#define REQ_CLASS_SET_HOOK_STATE                                0x11
#define REQ_CLASS_PULSE_SETUP                                   0x12
#define REQ_CLASS_SEND_PULSE                                    0x13
#define REQ_CLASS_SET_PULSE_TIME                                0x14
#define REQ_CLASS_RING_AUX_JACK                                 0x15
#define REQ_CLASS_SET_LINE_CODING                               0x20
#define REQ_CLASS_GET_LINE_CODING                               0x21
#define REQ_CLASS_SET_CONTROL_LINE_STATE                        0x22
#define REQ_CLASS_SEND_BREAK                                    0x23
#define REQ_CLASS_SET_RINGER_PARMS                              0x30
#define REQ_CLASS_GET_RINGER_PARMS                              0x31
#define REQ_CLASS_SET_OPERATION_PARMS                           0x32
#define REQ_CLASS_GET_OPERATION_PARMS                           0x33
#define REQ_CLASS_SET_LINE_PARMS                                0x34
#define REQ_CLASS_GET_LINE_PARMS                                0x35
#define REQ_CLASS_DIAL_DIGITS                                   0x36
#define REQ_CLASS_SET_UNIT_PARAMETER                            0x37
#define REQ_CLASS_GET_UNIT_PARAMETER                            0x38
#define REQ_CLASS_CLEAR_UNIT_PARAMETER                          0x39
#define REQ_CLASS_GET_PROFILE                                   0x3A
#define REQ_CLASS_SET_ETHERNET_MULTICAST_FILTERS                0x40
#define REQ_CLASS_SET_ETHERNET_POWER_MANAGEMENT_PATTERN_FILTER  0x41
#define REQ_CLASS_GET_ETHERNET_POWER_MANAGEMENT_PATTERN_FILTER  0x42
#define REQ_CLASS_SET_ETHERNET_PACKET_FILTER                    0x43
#define REQ_CLASS_GET_ETHERNET_STATISTIC                        0x44
#define REQ_CLASS_SET_ATM_DATA_FORMAT                           0x50
#define REQ_CLASS_GET_ATM_DEVICE_STATISTICS                     0x51
#define REQ_CLASS_SET_ATM_DEFAULT_VC                            0x52
#define REQ_CLASS_GET_ATM_VC_STATISTICS                         0x53
// #define REQ_CLASS_MDLM Semantic-Model specific Requests         0x60 - 0x7F
#define REQ_CLASS_GET_NTB_PARAMETERS                            0x80
#define REQ_CLASS_GET_NET_ADDRESS                               0x81
#define REQ_CLASS_SET_NET_ADDRESS                               0x82
#define REQ_CLASS_GET_NTB_FORMAT                                0x83
#define REQ_CLASS_SET_NTB_FORMAT                                0x84
#define REQ_CLASS_GET_NTB_INPUT_SIZE                            0x85
#define REQ_CLASS_SET_NTB_INPUT_SIZE                            0x86
#define REQ_CLASS_GET_MAX_DATAGRAM_SIZE                         0x87
#define REQ_CLASS_SET_MAX_DATAGRAM_SIZE                         0x88
#define REQ_CLASS_GET_CRC_MODE                                  0x89
#define REQ_CLASS_SET_CRC_MODE                                  0x8A

typedef struct PACKED {
    uint32_t dwDTERate;
    uint8_t bCharFormat;
    uint8_t bParityType;
    uint8_t bDataBits;
} line_coding_t;

static line_coding_t line_coding = {
    .dwDTERate = 115200,
    .bCharFormat = 0,       // 1 stop bit
    .bParityType = 0,       // Parity none
    .bDataBits = 8,         // Data bits
};

const void *usb_cdc_setup(setup_t *setup)
{
    switch (setup->bRequest) {
    case REQ_CLASS_SET_LINE_CODING:
        line_coding = *(const line_coding_t *)setup->data;
        return 0;

    case REQ_CLASS_GET_LINE_CODING:
        return &line_coding;

    case REQ_CLASS_SET_CONTROL_LINE_STATE: {
        const uint16_t state = setup->wValue;
        return 0;
    }

    default:
        DBG_BKPT("Unknown request");
        return (void *)-1;
    }
}
