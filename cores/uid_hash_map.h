#ifndef USER_ID_HASH_MAP
#define USER_ID_HASH_MAP

#include <pthread.h>
#include <stdbool.h>
#include "../utilities/uthash.h"

typedef struct int_hash_entry {
    int key;
    int value;
    size_t node_idx;
    UT_hash_handle hh;
} int_hash_entry_t;

typedef struct int_hash_map {
    size_t map_limit;
    pthread_mutex_t hash_map_mutex;
    
    size_t* node_pool_idx_stack;
    size_t idx_stack_top_idx;
    
    int_hash_entry_t* node_pool;
    int_hash_entry_t* hash_map;
} int_hash_map_t;

void init_hash_map(int_hash_map_t* hash_map_ptr, size_t map_limit);
bool insert(int_hash_map_t* hash_map_ptr, int key, int value);
int find(int_hash_map_t* hash_map_ptr, int key);
void erase(int_hash_map_t* hash_map_ptr, int key);
void clear_hash_map(int_hash_map_t* hash_map_ptr);
size_t get_all_keys(int_hash_map_t* hash_map_ptr, int** keys);

#endif