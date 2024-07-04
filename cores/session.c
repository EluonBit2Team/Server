#include "session.h"

void reset_session(client_session_t* session_ptr)
{
    session_ptr->fd = -1;
    pthread_mutex_lock(&session_ptr->send_buf_mutex);
    reset_queue(&session_ptr->send_bufs);
    pthread_mutex_unlock(&session_ptr->send_buf_mutex);
}

int init_session_pool(session_pool_t* pool_ptr, size_t session_size)
{
    pool_ptr->session_size = session_size;
    pool_ptr->session_pool = malloc(sizeof(client_session_t) * session_size);
    if (pool_ptr->session_pool == NULL) {
        // Todo: 로깅
        return -1;
    }

    pool_ptr->session_pool_idx_stack = malloc(sizeof(int) * session_size);
    if (pool_ptr->session_pool_idx_stack == NULL) {
        // Todo: 로깅
        return -1;
    }

    for (int i = 0; i < session_size; i++) {
        pool_ptr->session_pool[i].session_idx = i;
        pool_ptr->session_pool[i].fd = -1;
        init_queue(&pool_ptr->session_pool[i].send_bufs, sizeof(send_buf_t));
        ring_init(&pool_ptr->session_pool[i].recv_bufs);
        pool_ptr->session_pool_idx_stack[i] = i;
        pthread_mutex_init(&pool_ptr->session_pool[i].send_buf_mutex, NULL);
    }
    
    pool_ptr->stack_top_idx = session_size - 1;
    pool_ptr->hash_map_by_fd = NULL;
}

client_session_t* assign_session(session_pool_t* pool_ptr, int fd)
{
    if (pool_ptr->stack_top_idx == 0) {
        return NULL;
    }

    int empty_session_idx = pool_ptr->session_pool_idx_stack[pool_ptr->stack_top_idx--];
    client_session_t* empty_session_ptr = &pool_ptr->session_pool[empty_session_idx];
    empty_session_ptr->fd = fd;
    HASH_ADD_INT(pool_ptr->hash_map_by_fd, fd, empty_session_ptr);
    
    return empty_session_ptr;
}

client_session_t* find_session_by_fd(session_pool_t* pool_ptr, int fd)
{
    client_session_t* temp_session = NULL;
    HASH_FIND_INT(pool_ptr->hash_map_by_fd, &fd, temp_session);
    return temp_session;
}

int close_session(session_pool_t* pool_ptr, client_session_t* session)
{
    if (session == NULL) {
        printf("session is NULL\n");
        return 0;
    }
    if (session->fd == -1)
    {
        printf("Invalid session in close_session\n");
        return -1;
    }

    if (pool_ptr->stack_top_idx >= pool_ptr->session_size) {
        printf("invalid session pool num\n");
        return -1;
    }

    pool_ptr->session_pool_idx_stack[++pool_ptr->stack_top_idx] = session->session_idx;
    client_session_t* temp_session;
    HASH_FIND_INT(pool_ptr->hash_map_by_fd, &session->fd, temp_session);
    if (temp_session != NULL) {
        HASH_DEL(pool_ptr->hash_map_by_fd, session);
    }
    else {
        printf("session find error\n");
    }
    reset_session(session);
}

void close_all_sessions(int epoll_fd, session_pool_t* pool_ptr)
{
    for (int i = 0; i < MAX_CLIENT_NUM; i++) {
        if (pool_ptr->session_pool[i].fd != -1) {
            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, pool_ptr->session_pool[i].fd, NULL);
            close(pool_ptr->session_pool[i].fd);
            pthread_mutex_destroy(&pool_ptr->session_pool[i].send_buf_mutex);
        }
    }
    free(pool_ptr->session_pool);
    free(pool_ptr->session_pool_idx_stack);
}