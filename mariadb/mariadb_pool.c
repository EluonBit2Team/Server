#include "mariadb_pool.h"

bool init_mariadb_pool(mariadb_conn_pool_t* pool, size_t poolsize, const char* DB_NAME) {
    const int CONN_TIMEOUT_SEC = DB_CONN_TIMEOUT_SEC;
    sem_init(&pool->pool_sem, 0, poolsize);
    pool->poolsize = poolsize;
    pool->pool = (conn_t*)malloc(sizeof(conn_t) * poolsize);
    if (pool->pool == NULL) {
        // TODO 로깅
        return -1;
    }

    pool->pool_idx_stack = (int*)malloc(sizeof(int) * poolsize);
    if (pool->pool_idx_stack == NULL) {
        free(pool->pool);
        // TODO 로깅
        return -1;
    }

    for (int i = 0; i < poolsize; i++)
    {
        pool->pool[i].idx = i;
        pool->pool[i].conn = mysql_init(0);
        if (pool->pool[i].conn == NULL) {
            printf("%s conn init fail: %s\n", DB_NAME, mysql_error(pool->pool[i].conn));
            free(pool->pool);
            free(pool->pool_idx_stack);
            for (int j = 0; j < i; j++) {
                mysql_close(pool->pool[j].conn);
            }
            return false;
        }
        mysql_options(pool->pool[i].conn, MYSQL_OPT_CONNECT_TIMEOUT, &CONN_TIMEOUT_SEC);
        MYSQL* rt_ptr = mysql_real_connect(pool->pool[i].conn, DB_IP, DB_USER_NAME, DB_USER_PASS, DB_NAME, DB_PORT, NULL, 0);
        if (rt_ptr == NULL)
        {
            printf("%s conn fail: %s\n", DB_NAME, mysql_error(pool->pool[i].conn));
            free(pool->pool);
            free(pool->pool_idx_stack);
            for (int j = 0; j < i; j++) {
                mysql_close(pool->pool[j].conn);
            }
            return false;
        }

        if (mysql_set_character_set(pool->pool[i].conn, "utf8")) {
            printf("%s mysql_set_character_set fail: %s\n", DB_NAME, mysql_error(pool->pool[i].conn));
            free(pool->pool);
            free(pool->pool_idx_stack);
            for (int j = 0; j < i; j++) {
                mysql_close(pool->pool[j].conn);
            }
            return false;
        }
        pool->pool_idx_stack[i] = i;
    }
    pool->pool_idx_stack_top = poolsize - 1;
    return true;
}

void close_mariadb_pool(mariadb_conn_pool_t* pool)
{
    for (int i = 0; i < pool->poolsize; i++)
    {
        mysql_close(pool->pool[i].conn);
    }
    free(pool->pool);
    free(pool->pool_idx_stack);
    sem_destroy(&pool->pool_sem);
}

conn_t* get_conn(mariadb_conn_pool_t* pool)
{
    sem_wait(&pool->pool_sem);
    int availableIdx = pool->pool_idx_stack[pool->pool_idx_stack_top];
    --pool->pool_idx_stack_top;
    conn_t* rt = &pool->pool[availableIdx];
    sem_post(&pool->pool_sem);
    return rt;
}

void release_conn(mariadb_conn_pool_t* pool, conn_t* conn)
{
    sem_wait(&pool->pool_sem);
    ++pool->pool_idx_stack_top;
    pool->pool_idx_stack[pool->pool_idx_stack_top] = conn->idx;
    sem_post(&pool->pool_sem);
}