#include "ring_buffer.h"

void ring_buffer_init(ring_buffer_t* rb) {
    rb->head = 0;
    rb->tail = 0;
    rb->size = 0;
}

size_t ring_buffer_write(ring_buffer_t* rb, const char* data, size_t bytes) {
    size_t bytes_written = 0;
    while (bytes_written < bytes && rb->size < RING_BUFFER_SIZE) {
        rb->buffer[rb->head] = data[bytes_written];
        rb->head = (rb->head + 1) % RING_BUFFER_SIZE;
        rb->size++;
        bytes_written++;
    }
    return bytes_written;
}

size_t ring_buffer_read(ring_buffer_t* rb, char* data, size_t bytes) {
    size_t bytes_read = 0;
    while (bytes_read < bytes && rb->size > 0) {
        data[bytes_read] = rb->buffer[rb->tail];
        rb->tail = (rb->tail + 1) % RING_BUFFER_SIZE;
        rb->size--;
        bytes_read++;
    }
    return bytes_read;
}

size_t ring_buffer_available_data(ring_buffer_t* rb) {
    return rb->size;
}

size_t ring_buffer_available_space(ring_buffer_t* rb) {
    return RING_BUFFER_SIZE - rb->size;
}