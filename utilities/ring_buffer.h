#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdlib.h>
#include <string.h>

#define RING_BUFFER_SIZE 2048  // 링 버퍼 크기

typedef struct {
    char buffer[RING_BUFFER_SIZE];
    size_t head;
    size_t tail;
    size_t size;
} ring_buffer_t;

void ring_buffer_init(ring_buffer_t* rb);
size_t ring_buffer_write(ring_buffer_t* rb, const char* data, size_t bytes);
size_t ring_buffer_read(ring_buffer_t* rb, char* data, size_t bytes);
size_t ring_buffer_available_data(ring_buffer_t* rb);
size_t ring_buffer_available_space(ring_buffer_t* rb);

#endif