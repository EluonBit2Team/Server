#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

#define QUEUE_SIZE  1024
#define NEXT(index)   ((index+1)%QUEUE_SIZE)

typedef struct ring_buf
{
    char buf[QUEUE_SIZE];
    int front; 
    int rear;
}ring_buf;

void ring_clear(ring_buf *ring);
bool ring_full(ring_buf *ring);
bool ring_empty(ring_buf *ring);
void ring_enque(ring_buf *ring, char data);
char ring_deque(ring_buf *ring);
bool ring_array(ring_buf *queue, char *data_ptr, int length);
int ring_read(ring_buf *ring, int fd);

#endif
