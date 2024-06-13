#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>

#define MAX_BUFF_SIZE  2048
#define NEXT(index)   ((index+1) % MAX_BUFF_SIZE)
#define HEADER_SIZE sizeof(int)

typedef struct ring_buf
{
    char buf[MAX_BUFF_SIZE];
    int front;
    int rear;
    int msg_size;
}ring_buf;

void ring_init(ring_buf *ring);
bool ring_full(ring_buf *ring);
bool ring_empty(ring_buf *ring);
char ring_deque(ring_buf *ring);
bool ring_array(ring_buf *queue, char *data_ptr);
void ring_free(ring_buf *ring);
int ring_read(ring_buf *ring, int fd);
void set_ring_header(ring_buf *ring);
int get_ring_size(ring_buf *ring);

#endif
