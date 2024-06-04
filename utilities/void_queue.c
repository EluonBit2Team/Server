#include "void_queue.h"

#include "../cores/NetCore.h"

void init_queue(void_queue_t* queue, int type_default_size)
{
    queue->type_default_size = type_default_size;
    queue->rear_node = NULL;
}

int enqueue(void_queue_t* queue, const void* data_org)
{
    node_t* new_node = (node_t*)malloc(sizeof(void_queue_t));
    new_node->data = malloc(queue->type_default_size);
    if (new_node->data == NULL)
    {
        // TODO: error 로깅 및 처리
        return -1;
    }
    memcpy(new_node->data, data_org, queue->type_default_size);
    new_node->next = NULL;
    
    // queue의 rear과 새 노드를 연결
    if (queue->rear_node != NULL)
    {
        queue->rear_node->next = new_node;
    }
    new_node->pre = queue->rear_node;
    
    // rear노드 갱신
    queue->rear_node = new_node;
    return 0;
}

int dequeue(void_queue_t* queue, void* data_des)
{
    node_t* r_node = queue->rear_node;
    if (r_node == NULL)
    {
        return -1;
    }

    if (data_des != NULL)
    {
       memcpy(data_des, r_node->data, queue->type_default_size);
    }
    node_t* new_r_node = queue->rear_node->pre;
    queue->rear_node = new_r_node;
    if (new_r_node != NULL)
    {
        new_r_node->next = NULL;
    }

    free(r_node->data);
    free(r_node);
}

void* get_rear_data(void_queue_t* queue)
{
    return queue->rear_node->data;
}

bool is_empty(void_queue_t* queue)
{
    if (queue->rear_node == NULL)
    {
        return true;
    }
    return false;
}