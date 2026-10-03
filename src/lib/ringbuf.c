#include "types.h"
#include "ringbuf.h"


static inline uint32_t next_index(const ringbuf_t *rb, uint32_t idx) {
    if (rb->size != 0) {
        return (idx + 1u) % rb->size;
    }else{
        //panic("RING_BUFFER_SIZE_ZERO", NULL);
        return 0;
    }
}

void ringbuf_init(ringbuf_t *rb, uint8_t *storage, uint32_t size) {
    rb->head = 0;
    rb->tail = 0;
    rb->size = size;
    rb->buf  = storage;
}

bool ringbuf_is_empty(const ringbuf_t *rb) {
    return rb->head == rb->tail;
}

bool ringbuf_is_full(const ringbuf_t *rb) {
    return next_index(rb, rb->head) == rb->tail;
}

uint32_t ringbuf_capacity(const ringbuf_t *rb) {
    return rb->size - 1u; // one slot is always unused
}

uint32_t ringbuf_count(const ringbuf_t *rb) {
    if (rb->head >= rb->tail) {
        return rb->head - rb->tail;
    } else {
        return rb->size - (rb->tail - rb->head);
    }
}

bool ringbuf_push(ringbuf_t *rb, uint8_t byte) {
    uint32_t head = rb->head;
    uint32_t next = next_index(rb, head);

    if (next == rb->tail) {
        // full
        return false;
    }

    rb->buf[head] = byte;
    rb->head = next;
    return true;
}

bool ringbuf_pop(ringbuf_t *rb, uint8_t *out) {
    uint32_t tail = rb->tail;

    if (tail == rb->head) {
        // empty
        return false;
    }

    *out = rb->buf[tail];
    rb->tail = next_index(rb, tail);
    return true;
}

uint32_t ringbuf_write(ringbuf_t *rb, const uint8_t *data, uint32_t len) {
    uint32_t written = 0;
    while (written < len && !ringbuf_is_full(rb)) {
        ringbuf_push(rb, data[written]);
        written++;
    }
    return written;
}

uint32_t ringbuf_read(ringbuf_t *rb, uint8_t *data, uint32_t len) {
    uint32_t read = 0;
    while (read < len && !ringbuf_is_empty(rb)) {
        ringbuf_pop(rb, &data[read]);
        read++;
    }
    return read;
}
