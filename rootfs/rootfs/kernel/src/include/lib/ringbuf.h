#ifndef RINGBUF_H
#define RINGBUF_H

#include "types.h"


typedef struct ringbuf {
    volatile uint32_t head;   // write index (producer)
    volatile uint32_t tail;   // read index (consumer)
    uint32_t size;            // capacity in bytes
    uint8_t *buf;             // backing storage
} ringbuf_t;

// API
void     ringbuf_init(ringbuf_t *rb, uint8_t *storage, uint32_t size);
bool     ringbuf_is_empty(const ringbuf_t *rb);
bool     ringbuf_is_full(const ringbuf_t *rb);
uint32_t ringbuf_capacity(const ringbuf_t *rb);
uint32_t ringbuf_count(const ringbuf_t *rb);

// Single-byte operations
bool     ringbuf_push(ringbuf_t *rb, uint8_t byte);   // returns false if full
bool     ringbuf_pop(ringbuf_t *rb, uint8_t *out);    // returns false if empty

// Multi-byte operations (optional)
uint32_t ringbuf_write(ringbuf_t *rb, const uint8_t *data, uint32_t len);
uint32_t ringbuf_read(ringbuf_t *rb, uint8_t *data, uint32_t len);

#endif
