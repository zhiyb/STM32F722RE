#include "macros.h"
#include "semihosting.h"
#include "log.h"
#include "fifo.h"
#include "usb.h"
#include "usb_cmsis_dap.h"
#include "usb_internal.h"
#include "DAP_config.h"
#include "DAP.h"

static struct {
    struct {
        uint8_t in_buf[64] ALIGNED(4);
        uint8_t out_buf[64] ALIGNED(4);
    } if_fs;
    struct {
        uint8_t in_buf[512] ALIGNED(4);
        uint8_t out_buf[512] ALIGNED(4);
    } if_hs;
} usb_cdap_buffer SECTION(.dmaram);

typedef struct {
    bool ep_in_pending;
    bool ep_out_pending;
} usb_cdap_if_t;

static struct {
    usb_cdap_if_t ifs[UsbNumIfs];
    uint8_t req_buf[512 * DAP_PACKET_COUNT];
    uint8_t resp_buf[512 * DAP_PACKET_COUNT];
    fifo_t req_fifo;
    fifo_t resp_fifo;
} usb_cdap;

uint16_t dap_packet_size;

static inline uint8_t *usb_in_buf(usb_if_t usb_if)
{
    return usb_if == UsbIfHs ?
        &usb_cdap_buffer.if_hs.in_buf[0] :
        &usb_cdap_buffer.if_fs.in_buf[0];
}

static inline uint8_t *usb_out_buf(usb_if_t usb_if)
{
    return usb_if == UsbIfHs ?
        &usb_cdap_buffer.if_hs.out_buf[0] :
        &usb_cdap_buffer.if_fs.out_buf[0];
}

void usb_cmsis_dap_init()
{
    fifo_init(&usb_cdap.req_fifo, usb_cdap.req_buf, sizeof(usb_cdap.req_buf));
    fifo_init(&usb_cdap.resp_fifo, usb_cdap.resp_buf, sizeof(usb_cdap.resp_buf));
    DAP_Setup();
}

void usb_cmsis_dap_ep_init(usb_if_t usb_if)
{
    fifo_init(&usb_cdap.req_fifo, usb_cdap.req_buf, sizeof(usb_cdap.req_buf));
    fifo_init(&usb_cdap.resp_fifo, usb_cdap.resp_buf, sizeof(usb_cdap.resp_buf));
    usb_cdap.ifs[usb_if] = (usb_cdap_if_t){0};

    // Ready to receive a new packet
    usb_t *usb = &usb_ifs[usb_if];
    usb_cdap_if_t *cdap = &usb_cdap.ifs[usb_if];
    uint8_t *buf = usb_out_buf(usb_if);
    cdap->ep_out_pending = true;
    usb_hw_ep_out(usb_if, UsbEpOutCmsisDap, buf, 0, 1, usb->ep.out[UsbEpOutCmsisDap].max_size);
}

void usb_cmsis_dap_ep_out(usb_if_t usb_if)
{
    // Copy received packet to FIFO
    usb_t *usb = &usb_ifs[usb_if];
    usb_cdap_if_t *cdap = &usb_cdap.ifs[usb_if];
    uint32_t req_len = usb->ep.out[UsbEpOutCmsisDap].offset;
    uint8_t *req_buf = usb_out_buf(usb_if);
    fifo_push(&usb_cdap.req_fifo, req_buf, req_len);
    cdap->ep_out_pending = false;

    uint32_t req_free = fifo_free(&usb_cdap.req_fifo);
    DAP_PACKET_SIZE = usb->ep.out[UsbEpOutCmsisDap].max_size;
    if (req_free >= DAP_PACKET_SIZE) {
        // There is enough request free space to receive another packet
        cdap->ep_out_pending = true;
        usb_hw_ep_out(usb_if, UsbEpOutCmsisDap, req_buf, 0, 1, DAP_PACKET_SIZE);
    }

    while (fifo_avail(&usb_cdap.req_fifo) && fifo_free(&usb_cdap.resp_fifo) >= DAP_PACKET_SIZE) {
        // There is enough response buffer space to process a packet
        uint8_t req[DAP_PACKET_SIZE];
        uint8_t resp[DAP_PACKET_SIZE];
        fifo_peek(&usb_cdap.req_fifo, req, DAP_PACKET_SIZE);
        uint32_t num = DAP_ExecuteCommand(req, resp);
        uint16_t processed_resp_len = num & 0xffff;
        uint16_t processed_req_len = (num >> 16) & 0xffff;
        fifo_drop(&usb_cdap.req_fifo, processed_req_len);
        fifo_push(&usb_cdap.resp_fifo, resp, processed_resp_len);
        if (!processed_req_len)
            break;
    }

    if (!cdap->ep_in_pending && fifo_avail(&usb_cdap.resp_fifo)) {
        // There is response data to transmit and IN endpoint is idling
        uint8_t *resp_buf = usb_in_buf(usb_if);
        uint32_t resp_len = fifo_pop(&usb_cdap.resp_fifo, resp_buf, DAP_PACKET_SIZE);
        cdap->ep_in_pending = true;
        usb_hw_ep_in(usb_if, UsbEpInCmsisDap, resp_buf, resp_len, false);
    }
}

void usb_cmsis_dap_ep_in(usb_if_t usb_if)
{
    // We can drop transmitted bytes now
    usb_t *usb = &usb_ifs[usb_if];
    usb_cdap_if_t *cdap = &usb_cdap.ifs[usb_if];
    uint32_t resp_len = usb->ep.in[UsbEpInCmsisDap].offset;
    fifo_drop(&usb_cdap.resp_fifo, resp_len);
    cdap->ep_in_pending = false;
    DAP_PACKET_SIZE = usb->ep.in[UsbEpInCmsisDap].max_size;

    if (fifo_avail(&usb_cdap.resp_fifo)) {
        // There is response data to transmit and IN endpoint is idling
        uint8_t *resp_buf = usb_in_buf(usb_if);
        uint32_t resp_len = fifo_pop(&usb_cdap.resp_fifo, resp_buf, DAP_PACKET_SIZE);
        cdap->ep_in_pending = true;
        usb_hw_ep_in(usb_if, UsbEpInCmsisDap, resp_buf, resp_len, false);
    }

    while (fifo_avail(&usb_cdap.req_fifo) && fifo_free(&usb_cdap.resp_fifo) >= DAP_PACKET_SIZE) {
        // There is enough response buffer space to process a packet
        uint8_t req[DAP_PACKET_SIZE];
        uint8_t resp[DAP_PACKET_SIZE];
        fifo_peek(&usb_cdap.req_fifo, req, DAP_PACKET_SIZE);
        uint32_t num = DAP_ExecuteCommand(req, resp);
        uint16_t processed_resp_len = num & 0xffff;
        uint16_t processed_req_len = (num >> 16) & 0xffff;
        fifo_drop(&usb_cdap.req_fifo, processed_req_len);
        fifo_push(&usb_cdap.resp_fifo, resp, processed_resp_len);
        if (!processed_req_len)
            break;
    }

    if (!cdap->ep_out_pending && fifo_free(&usb_cdap.req_fifo) >= DAP_PACKET_SIZE) {
        // There is enough request buffer space to receive and OUT endpoint is idling
        uint8_t *req_buf = usb_out_buf(usb_if);
        cdap->ep_out_pending = true;
        usb_hw_ep_out(usb_if, UsbEpOutCmsisDap, req_buf, 0, 1, DAP_PACKET_SIZE);
    }
}
