#pragma once
#include <stdint.h>
#include <string.h>

typedef struct {
    void *buf;
    uint32_t size;
    uint32_t wptr;
    uint32_t rptr;
} fifo_t;

static inline uint32_t fifo_avail(fifo_t *fifo)
{
    return (fifo->size + fifo->wptr - fifo->rptr) % fifo->size;
}

static inline uint32_t fifo_free(fifo_t *fifo)
{
    return (fifo->size - 1 - fifo->wptr + fifo->rptr) % fifo->size;
}

static inline void fifo_init(fifo_t *fifo, void *buf, uint32_t size)
{
    fifo->buf = buf;
    fifo->size = size;
}

static inline uint32_t fifo_push(fifo_t *fifo, const void *src, uint32_t len)
{
    uint32_t cp_len = fifo_free(fifo);
    cp_len = cp_len >= len ? len : cp_len;
    uint32_t back_free = fifo->size - fifo->wptr;
    uint32_t back_cp_len = back_free >= cp_len ? cp_len : back_free;
    memcpy(fifo->buf + fifo->wptr, src, back_cp_len);
    memcpy(fifo->buf, src + back_cp_len, cp_len - back_cp_len);
    fifo->wptr = (fifo->wptr + cp_len) % fifo->size;
    return cp_len;
}

static inline uint32_t fifo_peek(fifo_t *fifo, void *dst, uint32_t len)
{
    uint32_t cp_len = fifo_avail(fifo);
    cp_len = cp_len >= len ? len : cp_len;
    uint32_t back_avail = fifo->size - fifo->rptr;
    uint32_t back_cp_len = back_avail >= cp_len ? cp_len : back_avail;
    memcpy(dst, fifo->buf + fifo->rptr, back_cp_len);
    memcpy(dst + back_avail, fifo->buf, cp_len - back_cp_len);
    return cp_len;
}

static inline uint32_t fifo_drop(fifo_t *fifo, uint32_t len)
{
    uint32_t cp_len = fifo_avail(fifo);
    cp_len = cp_len >= len ? len : cp_len;
    fifo->rptr = (fifo->rptr + cp_len) % fifo->size;
    return cp_len;
}

static inline uint32_t fifo_pop(fifo_t *fifo, void *dst, uint32_t len)
{
    uint32_t cp_len = fifo_peek(fifo, dst, len);
    fifo->rptr = (fifo->rptr + cp_len) % fifo->size;
    return cp_len;
}
