#ifndef USER_ID_HASH_MAP
#define USER_ID_HASH_MAP

#include <pthread.h>
#include <stdbool.h>
#include "../utilities/uthash.h"

typedef struct uid_node {
    int fd_key;
    int uid_value;
    size_t node_idx;
    UT_hash_handle hh;
} uid_node_t;

typedef struct uid_hash_map {
    size_t map_limit;
    pthread_mutex_t hash_map_mutex;
    
    size_t* node_pool_idx_stack;
    size_t idx_stack_top_idx;
    
    uid_node_t* node_pool;
    uid_node_t* hash_map;
} uid_hash_map_t;

void init_hash_map(uid_hash_map_t* hash_map_ptr, size_t map_limit);
bool insert(uid_hash_map_t* hash_map_ptr, int fd_key, int uid_value);
int find(uid_hash_map_t* hash_map_ptr, int fd_key);
void erase(uid_hash_map_t* hash_map_ptr, int fd_key);
void clear_hash_map(uid_hash_map_t* hash_map_ptr);

#endif