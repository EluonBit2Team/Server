#include "uid_hash_map.h"

void init_hash_map(uid_hash_map_t* hash_map_ptr, size_t map_limit) {
    pthread_mutex_init(&hash_map_ptr->hash_map_mutex, NULL);
    hash_map_ptr->map_limit = map_limit;
    hash_map_ptr->node_pool = (uid_node_t*)malloc(sizeof(uid_node_t) * map_limit);
    hash_map_ptr->node_pool_idx_stack = (size_t*)malloc(sizeof(size_t) * map_limit);
    if (hash_map_ptr->node_pool == NULL || hash_map_ptr->node_pool_idx_stack == NULL)
    {
        return ;
    }
    for (size_t i = 0; i < map_limit; i++)
    {
        hash_map_ptr->node_pool_idx_stack[i] = i;
        hash_map_ptr->node_pool[i].node_idx = i;
    }
    hash_map_ptr->idx_stack_top_idx = map_limit - 1;
    hash_map_ptr->hash_map = NULL;
}

bool insert(uid_hash_map_t* hash_map_ptr, int fd_key, int uid_value) {
    pthread_mutex_lock(&hash_map_ptr->hash_map_mutex);
    if (hash_map_ptr->idx_stack_top_idx == 0)
    {
        pthread_mutex_unlock(&hash_map_ptr->hash_map_mutex);
        return false;
    }
    size_t empty_node_idx = hash_map_ptr->node_pool_idx_stack[hash_map_ptr->idx_stack_top_idx--];
    uid_node_t* empty_node = &hash_map_ptr->node_pool[empty_node_idx];
    empty_node->fd_key = fd_key;
    empty_node->uid_value = uid_value;
    HASH_ADD_INT(hash_map_ptr->hash_map, fd_key, empty_node);
    pthread_mutex_unlock(&hash_map_ptr->hash_map_mutex);
    return true;
}

int find(uid_hash_map_t* hash_map_ptr, int fd_key) {
    pthread_mutex_lock(&hash_map_ptr->hash_map_mutex);
    uid_node_t* value = NULL;
    HASH_FIND_INT(hash_map_ptr->hash_map, &fd_key, value);
    if (value == NULL) {
        pthread_mutex_unlock(&hash_map_ptr->hash_map_mutex);
        return -1;
    }
    pthread_mutex_unlock(&hash_map_ptr->hash_map_mutex);
    return value->uid_value;
}

void erase(uid_hash_map_t* hash_map_ptr, int fd_key) {
    pthread_mutex_lock(&hash_map_ptr->hash_map_mutex);
    uid_node_t* value = NULL;
    HASH_FIND_INT(hash_map_ptr->hash_map, &fd_key, value);
    if (value == NULL) {
        pthread_mutex_unlock(&hash_map_ptr->hash_map_mutex);
        return ;
    }
    ++hash_map_ptr->idx_stack_top_idx;
    hash_map_ptr->node_pool_idx_stack[hash_map_ptr->idx_stack_top_idx] = value->node_idx;
    HASH_DEL(hash_map_ptr->hash_map, value);
    pthread_mutex_unlock(&hash_map_ptr->hash_map_mutex);
}

void clear_hash_map(uid_hash_map_t* hash_map_ptr) {
    pthread_mutex_destroy(&hash_map_ptr->hash_map_mutex);
    free(hash_map_ptr->node_pool);
    free(hash_map_ptr->node_pool_idx_stack);
}