#include "usb.h"
#include "usb_desc_hid.h"
#include "semihosting.h"

// Standard requests
#define REQ_STD_GET_DESCRIPTOR  6
#define REQ_STD_SET_DESCRIPTOR  7

// Class-specific requests
#define REQ_CLASS_GET_REPORT      0x01
#define REQ_CLASS_GET_IDLE        0x02
#define REQ_CLASS_GET_PROTOCOL    0x03
#define REQ_CLASS_SET_REPORT      0x09
#define REQ_CLASS_SET_IDLE        0x0a
#define REQ_CLASS_SET_PROTOCOL    0x0b

const void *usb_hid_setup(setup_t *setup)
{
    switch (setup->bRequest) {
    case REQ_STD_GET_DESCRIPTOR: {
        // bmRequestType == 0x81
        uint8_t type = setup->wValue >> 8;
        uint8_t index = setup->wValue;
        if (type != DESC_TYPE_HID_REPORT) {
            DBG_BKPT("Unknown descriptor");
            return (void *)-1;
        }
        const uint16_t len = sizeof(hidReportDescriptor);
        setup->wLength = len < setup->wLength ? len : setup->wLength;
        return hidReportDescriptor;
    }

    case REQ_CLASS_SET_IDLE: {
        // bmRequestType == 0x21
        // The Set_Idle request silences a particular report on the Interrupt In pipe
        // until a new event occurs or the specified amount of time passes.
        uint8_t duration = setup->wValue >> 8;
        uint8_t id = setup->wValue;
        // TODO();
        return 0;
    }

    default:
        DBG_BKPT("Unknown request");
        return (void *)-1;
    }
}

static union {
    struct HidReportInput1 keyboard;
    struct HidReportInput2 mouse;
    struct HidReportInput3 vendor;
} hid_report ALIGNED(4);

typedef enum {
    HidReportIdKeyboard = HID_REPORT_INPUT1_ID,
    HidReportIdMouse = HID_REPORT_INPUT2_ID,
    HidReportIdVendor = HID_REPORT_INPUT3_ID,
} hid_report_id_t;

static bool periodic_move = true;

void usb_hid_process(uint32_t now_ms)
{
    if (!periodic_move)
        return;
    if (!usb_is_connected())
        return;

    // Move mouse down every 1 sec
    static uint32_t last_ms = 0;
    uint32_t delta_ms = now_ms - last_ms;
    if (delta_ms < 1000)
        return;
    if (delta_ms >= 2000)
        last_ms = now_ms;   // Discontinuity
    else
        last_ms += 1000;

    // Create mouse event report
    hid_report.mouse.ReportId = HidReportIdMouse;
    hid_report.mouse.Payload[0] = 10;   // X +ve: right
    hid_report.mouse.Payload[1] = 10;   // Y +ve: down
    hid_report.mouse.Payload[2] = 0;    // Buttons
    // dbg_puts("Mouse event\r\n");
    if (usb_hw_ep_tx_status(UsbEpHid) != UsbEpValid)
        usb_hw_ep_tx(UsbEpHid, &hid_report, sizeof(hid_report.mouse), false);
}

void usb_hid_mouse_move(int8_t x, int8_t y)
{
    periodic_move = false;
    // Create mouse event report
    hid_report.mouse.ReportId = HidReportIdMouse;
    hid_report.mouse.Payload[0] = x;    // X +ve: right
    hid_report.mouse.Payload[1] = y;    // Y +ve: down
    hid_report.mouse.Payload[2] = 0;    // Buttons
    if (usb_hw_ep_tx_status(UsbEpHid) != UsbEpValid)
        usb_hw_ep_tx(UsbEpHid, &hid_report, sizeof(hid_report.mouse), false);
}
