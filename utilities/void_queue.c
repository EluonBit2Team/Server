#include "void_queue.h"
#include "../cores/NetCore.h"

void init_queue(void_queue_t* queue, int type_default_size) {
    queue->type_default_size = type_default_size;
    queue->pront_node = NULL;
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
    queue->rear_node = NULL;
}

int enqueue(void_queue_t* queue, const void* data_org) {
    node_t* new_node = (node_t*)malloc(sizeof(node_t));
    new_node->data = malloc(queue->type_default_size);
    if (new_node->data == NULL) {
        // TODO: error 로깅 및 처리
        return -1;
    }
    memcpy(new_node->data, data_org, queue->type_default_size);
    
    // new_node->pre = queue->rear_node;
    
    // // rear노드 갱신
    // queue->rear_node = new_node;

    // [queue] pre(new__node)rear <- pre()
    new_node->pre = NULL;
    new_node->next = queue->rear_node;
    if (new_node->next == NULL) {
        if (queue->pront_node != NULL) {
            fprintf(stderr, "%s", "Invalid queue node\n");
        }
        queue->pront_node = new_node;
    }
    else {
        queue->rear_node->pre = new_node;
    }
    queue->rear_node = new_node;
    return 0;
}

int dequeue(void_queue_t* queue, void* data_des) {
    // node_t* r_node = queue->rear_node;
    // if (r_node == NULL) {
    //     return -1;
    // }

    // if (data_des != NULL) {
    //    memcpy(data_des, r_node->data, queue->type_default_size);
    // }
    // node_t* new_r_node = queue->rear_node->pre;
    // queue->rear_node = new_r_node;

    node_t* f_node = queue->pront_node;
    if (f_node == NULL) {
        return -1;
    }

    if (data_des != NULL) {
       memcpy(data_des, f_node->data, queue->type_default_size);
    }
    
    queue->pront_node = f_node->next;
    if (queue->pront_node == NULL) {
        queue->rear_node = NULL;
        printf("queue->pront_node:NULL");
    }
    else {
        queue->pront_node->pre = NULL;
        printf("queue->pront_node:%p\n", queue->pront_node);
    }

    free(f_node->data);
    free(f_node);
    return 0;
}

void* get_front_node(void_queue_t* queue) {
    return queue->rear_node;
}

bool is_empty(void_queue_t* queue) {
    if (queue->rear_node == NULL) {
        return true;
    }
    return false;
}