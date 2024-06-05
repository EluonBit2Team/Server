#ifndef SESSION_H
#define SESSION_H

#include "../utilities/ring_buffer.h"
#include "../utilities/uthash.h"
#include "../utilities/void_queue.h"
#include "../defines.h"

typedef struct client_session {
    int fd;                     // 세션 fd
    size_t session_idx;
    ring_buf recv_bufs;
    void_queue_t send_bufs;
    UT_hash_handle hh;
} client_session_t;

typedef struct session_pool {
    size_t session_size;
    client_session_t* session_pool;

    int* session_pool_idx_stack;
    size_t stack_top_idx;

    client_session_t* hash_map_by_fd;
} session_pool_t;

void reset_session(client_session_t* session_ptr);
int init_session(session_pool_t* pool_ptr, size_t session_size);
client_session_t* assign_session(session_pool_t* pool_ptr, int fd);
client_session_t* find_session_by_fd(session_pool_t* pool_ptr, int fd);
int release_session(session_pool_t* pool_ptr, client_session_t* session);

#endif