#ifndef MARIADB_POOL_H
#define MARIADB_POOL_H

#include <semaphore.h>
#include <mysql/mysql.h>
#include "../includes.h"

#define DB_IP "192.168.0.253"
#define DB_PORT 3307
#define DB_USER_NAME "today_chicken"
#define DB_USER_PASS "1q2w3e4r"
#define DB_CONN_TIMEOUT_SEC 3

typedef struct conn {
    size_t idx;
    int db_idx;
    MYSQL* conn;
} conn_t;

typedef struct mariadb_conn_pool {
    size_t poolsize;
    conn_t* pool;
    int* pool_idx_stack;
    size_t pool_idx_stack_top;
    sem_t pool_sem;
    pthread_mutex_t pool_idx_mutex;
} mariadb_conn_pool_t;

bool init_mariadb_pool(mariadb_conn_pool_t* pool, size_t poolsize, int db_idx, const char* DB_NAME);
void close_mariadb_pool(mariadb_conn_pool_t* pool);
conn_t* get_conn(mariadb_conn_pool_t* pool);
void release_conn(mariadb_conn_pool_t* pool, conn_t* conn);

#endif