#ifndef MARIADB_H
#define MARIADB_H

#include <stdio.h>
#include <pthread.h>

#include "mariadb_pool.h"

#define USER_SETTING_DB "user_setting_db"
#define CHAT_GROUP_DB "chat_group_db"
#define STATISTIC_DB "server_statistic_db"
//#define USER_REQUEST_DB "user_request_db"
#define LOG_DB "log_db"

#define TOTAL_DB_NUM 4
#define USER_SETTING_DB_IDX 0
#define CHAT_GROUP_DB_IDX 1
#define STATISTIC_DB_IDX 2
#define LOG_DB_IDX 3
//#define USER_REQUEST_DB_IDX 3

typedef struct chatdb {
    mariadb_conn_pool_t pools[TOTAL_DB_NUM];
    char* db_names[TOTAL_DB_NUM];
    int db_sizes[TOTAL_DB_NUM];
} chatdb_t;

bool has_query_results(conn_t* conn, char** msg, const char* query);
cJSON* execute_query(conn_t* conn, char** msg, const char* query, int key_num, ...);
void release_conns(chatdb_t* db, int release_conn_num, ...);
bool init_mariadb(chatdb_t* db);
void close_mariadb(chatdb_t* db);
#endif