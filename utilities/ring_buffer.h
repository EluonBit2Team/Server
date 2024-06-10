#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

#define MIN_BUFF_SIZE  512
#define MAX_BUFF_SIZE  2048
#define NEXT(index, size)   ((index+1)%size)

typedef struct ring_buf
{
    char *buf;
    int front;
    int rear;
    int size;
}ring_buf;

void ring_init(ring_buf *ring);
void ring_resize(ring_buf *ring, int data_size);
void ring_clear(ring_buf *ring);
bool ring_full(ring_buf *ring);
bool ring_empty(ring_buf *ring);
void ring_enque(ring_buf *ring, char data);
char ring_deque(ring_buf *ring);
bool ring_array(ring_buf *queue, char *data_ptr, int length);
void ring_free(ring_buf *ring);
int ring_read(ring_buf *ring, int fd);

#endif
