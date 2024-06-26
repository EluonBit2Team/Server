#include "uid_hash_map.h"

void init_hash_map(int_hash_map_t* hash_map_ptr, size_t map_limit) {
    pthread_mutex_init(&hash_map_ptr->hash_map_mutex, NULL);
    hash_map_ptr->map_limit = map_limit;
    hash_map_ptr->node_pool = (int_hash_entry_t*)malloc(sizeof(int_hash_entry_t) * map_limit);
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

bool insert(int_hash_map_t* hash_map_ptr, int key, int value) {
    pthread_mutex_lock(&hash_map_ptr->hash_map_mutex);
    if (hash_map_ptr->idx_stack_top_idx == 0)
    {
        pthread_mutex_unlock(&hash_map_ptr->hash_map_mutex);
        return false;
    }
    size_t empty_node_idx = hash_map_ptr->node_pool_idx_stack[hash_map_ptr->idx_stack_top_idx--];
    int_hash_entry_t* empty_entry = &hash_map_ptr->node_pool[empty_node_idx];
    empty_entry->key = key;
    empty_entry->value = value;
    HASH_ADD_INT(hash_map_ptr->hash_map, key, empty_entry);
    pthread_mutex_unlock(&hash_map_ptr->hash_map_mutex);
    return true;
}

int find(int_hash_map_t* hash_map_ptr, int key) {
    pthread_mutex_lock(&hash_map_ptr->hash_map_mutex);
    int_hash_entry_t* find_entry = NULL;
    HASH_FIND_INT(hash_map_ptr->hash_map, &key, find_entry);
    if (find_entry == NULL) {
        pthread_mutex_unlock(&hash_map_ptr->hash_map_mutex);
        return -1;
    }
    pthread_mutex_unlock(&hash_map_ptr->hash_map_mutex);
    return find_entry->value;
}

void erase(int_hash_map_t* hash_map_ptr, int key) {
    pthread_mutex_lock(&hash_map_ptr->hash_map_mutex);
    int_hash_entry_t* find_entry = NULL;
    HASH_FIND_INT(hash_map_ptr->hash_map, &key, find_entry);
    if (find_entry == NULL) {
        pthread_mutex_unlock(&hash_map_ptr->hash_map_mutex);
        return ;
    }
    ++hash_map_ptr->idx_stack_top_idx;
    hash_map_ptr->node_pool_idx_stack[hash_map_ptr->idx_stack_top_idx] = find_entry->node_idx;
    HASH_DEL(hash_map_ptr->hash_map, find_entry);
    pthread_mutex_unlock(&hash_map_ptr->hash_map_mutex);
}

void clear_hash_map(int_hash_map_t* hash_map_ptr) {
    // HASH_CLEAR??
    pthread_mutex_destroy(&hash_map_ptr->hash_map_mutex);
    free(hash_map_ptr->node_pool);
    free(hash_map_ptr->node_pool_idx_stack);
}