#ifndef MARIADB_H
#define MARIADB_H

#include <stdio.h>
#include <pthread.h>

#include "mariadb_pool.h"
#include "../defines.h"

#define USER_SETTING_DB "user_setting_db"
#define CHAT_GROUP_DB "chat_group_db"
#define STATISTIC_DB "server_statistic_db"
#define LOG_DB "log_db"

#define TOTAL_DB_NUM 4
#define USER_SETTING_DB_IDX 0
#define CHAT_GROUP_DB_IDX 1
#define STATISTIC_DB_IDX 2
#define LOG_DB_IDX 3

typedef struct chatdb {
    mariadb_conn_pool_t pools[TOTAL_DB_NUM];
    char* db_names[TOTAL_DB_NUM];
    int db_sizes[TOTAL_DB_NUM];
} chatdb_t;

int query_result_to_int(conn_t* conn, char** out_msg, const char* query);
bool query_result_to_bool(conn_t* conn, char** out_msg, const char* query);
bool query_result_to_execuete(conn_t* conn, char** out_msg, const char* query);
cJSON* query_result_to_json(conn_t* conn, char** out_msg, const char* query, int key_num, ...);
cJSON* query_result_get_user_json(conn_t* conn, char** out_msg, const char* query, char* pass);
void release_conns(chatdb_t* db, int release_conn_num, ...);
bool init_mariadb(chatdb_t* db);
void close_mariadb(chatdb_t* db);
char* query_result_to_str(conn_t* conn, char** out_msg, const char* query);

#endif