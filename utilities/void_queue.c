#include "void_queue.h"
#include "../cores/NetCore.h"

void init_queue(void_queue_t* queue, int type_default_size) {
    queue->type_default_size = type_default_size;
    queue->front_node = NULL;
    queue->rear_node = NULL;
}

void reset_queue(void_queue_t* queue) {
    node_t* cur = queue->rear_node;
    while (cur != NULL) {
        node_t* temp_pre = cur->pre;
        free(cur->data);
        free(cur);
        cur = temp_pre;
    }
    queue->front_node = NULL;
    queue->rear_node = NULL;
}

int enqueue(void_queue_t* queue, const void* data_org) {
    node_t* new_node = (node_t*)malloc(sizeof(node_t));
    if (new_node == NULL) {
        printf("enqueue malloc fail\n");
        return -1;
    }
    new_node->data = malloc(queue->type_default_size);
    if (new_node->data == NULL) {
        free(new_node);
        printf("enqueue malloc fail\n");
        return -1;
    }

    memcpy(new_node->data, data_org, queue->type_default_size);
    new_node->pre = queue->rear_node;
    new_node->next = NULL;

    if (queue->rear_node == NULL) {
        if (queue->front_node != NULL) {
            fprintf(stderr, "%s", "Invalid queue node\n");
        }
        queue->front_node = new_node;
    }
    else {
        queue->rear_node->next = new_node;
    }
    queue->rear_node = new_node;

    //printf("front:%p, rear:%p /\nnew_node:%p data:%p pre:%p next:%p\n", queue->front_node, queue->rear_node, new_node, new_node->data, new_node->pre, new_node->next);
    return 0;
}

int dequeue(void_queue_t* queue, void* data_des) {
    node_t* f_node = queue->front_node;
    if (f_node == NULL) {
        return -1;
    }

    if (data_des != NULL) {
       memcpy(data_des, f_node->data, queue->type_default_size);
    }
    
    queue->front_node = f_node->next;
    if (queue->front_node == NULL) {
        queue->rear_node = NULL;
    }
    else {
        queue->front_node->pre = NULL;
    }
    //printf("front:%p, rear:%p /front_node:%p data:%p pre:%p next:%p\n", queue->front_node, queue->rear_node, f_node, f_node->data, f_node->pre, f_node->next);
    free(f_node->data);
    free(f_node);
    return 0;
}

void* get_front_node(void_queue_t* queue) {
    return queue->front_node;
}

bool is_empty(void_queue_t* queue) {
    if (queue->front_node == NULL) {
        return true;
    }
    return false;
}