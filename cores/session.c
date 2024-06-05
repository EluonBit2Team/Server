#include "session.h"

void reset_session(client_session_t* session_ptr)
{
    session_ptr->fd = -1;
    //ring_init(&session_ptr->recv_buf);
    reset_queue(&session_ptr->send_bufs);
}

int init_session(session_pool_t* pool_ptr, size_t session_size)
{
    pool_ptr->session_size = session_size;
    pool_ptr->session_pool = malloc(sizeof(client_session_t) * session_size);
    if (pool_ptr->session_pool == NULL)
    {
        // Todo: 로깅
        return -1;
    }
    pool_ptr->session_pool_idx_stack = malloc(sizeof(int) * session_size);
    if (pool_ptr->session_pool_idx_stack == NULL)
    {
        // Todo: 로깅
        return -1;
    }

    for (int i = 0; i < session_size; i++)
    {
        pool_ptr->session_pool[i].session_idx = i;
        pool_ptr->session_pool[i].fd = -1;
        //init pool_ptr->session_pool[i].recv_buf
        init_queue(&pool_ptr->session_pool[i].send_bufs, BUFF_SIZE);

        pool_ptr->session_pool_idx_stack[i] = i;
    }
    pool_ptr->stack_top_idx = session_size - 1;
}

client_session_t* assign_session(session_pool_t* pool_ptr, int fd)
{
    if (pool_ptr->stack_top_idx == 0)
    {
        return NULL;
    }
    int empty_session_idx = pool_ptr->session_pool_idx_stack[pool_ptr->stack_top_idx--];
    client_session_t* empty_session_ptr = &pool_ptr->session_pool[empty_session_idx];
    empty_session_ptr->fd = fd;

    HASH_ADD_INT(pool_ptr->hash_map_by_fd, fd, empty_session_ptr);
}

client_session_t* get_session_by_fd(session_pool_t* pool_ptr, int fd)
{
    client_session_t* temp_session = NULL;
    HASH_FIND_INT(pool_ptr->sessions_by_fd, &fd, temp_session);
    return temp_session;
}

int release_session(session_pool_t* pool_ptr, client_session_t* session)
{
    if (pool_ptr->stack_top_idx >= pool_ptr->session_size)
    {
        return -1;
    }
    pool_ptr->session_pool_idx_stack[++pool_ptr->stack_top_idx] = session->session_idx;
    client_session_t* temp_session;
    HASH_FIND_INT(pool_ptr->sessions_by_fd, &session->fd, temp_session);
    if (temp_session != NULL)
    {
        HASH_DEL(pool_ptr->hash_map_by_fd, session);
    }
    reset_session(session);
}