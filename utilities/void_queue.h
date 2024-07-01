#ifndef VOID_QUEUE_H
#define VOID_QUEUE_H

#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

typedef struct node {
    struct node* pre;
    struct node* next;
    void* data;
} node_t;

typedef struct void_queue {
    node_t* pront_node;
    node_t* rear_node;
    int type_default_size;
} void_queue_t;

void init_queue(void_queue_t* queue, int type_default_size);
void reset_queue(void_queue_t* queue);
int enqueue(void_queue_t* queue, const void* data_org);
int dequeue(void_queue_t* queue, void* data_des);
bool is_empty(void_queue_t* queue);
void* get_rear_data(void_queue_t* queue);
#endif