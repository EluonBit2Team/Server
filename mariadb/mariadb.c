#include "mariadb.h"

void release_conns(chatdb_t* db, int release_conn_num, ...) {
    va_list VA_LIST;
    va_start(VA_LIST, release_conn_num);
    for (int i = 0; i < release_conn_num; i++) {
        conn_t* conn = va_arg(VA_LIST, conn_t*);
        if (conn == NULL) {
            continue;
        }

        release_conn(&db->pools[conn->db_idx], conn);
    }
}

bool has_query_results(conn_t* conn, char** msg, const char* query) {
    MYSQL_RES *is_mng_query_result = NULL;
    MYSQL_ROW row;
    if (mysql_query(conn->conn, query)) {
        fprintf(stderr, "query fail: %s\n", mysql_error(conn->conn));
        *msg = "DB error";
        return false;
    }
    is_mng_query_result = mysql_store_result(conn->conn);
    if (is_mng_query_result == NULL) {
        fprintf(stderr, "mysql_store_result failed: %s\n", mysql_error(conn->conn));
        *msg = "DB error";
        return false;
    }
    if ((row = mysql_fetch_row(is_mng_query_result)) == NULL) {
        *msg = "invalid user role";
        return false;
    }
    mysql_free_result(is_mng_query_result);
    return true;
}

cJSON* execute_query(conn_t* conn, char** msg, const char* query, int key_num, ...) {
    va_list VA_LIST;
    MYSQL_ROW row;
    MYSQL_RES *signup_req_query_result = NULL;

    if (mysql_query(conn->conn, query)) {
        fprintf(stderr, "query fail: %s\n", mysql_error(conn->conn));
        *msg = "DB error";
        return NULL;
    }
    signup_req_query_result = mysql_store_result(conn->conn);
    if (signup_req_query_result == NULL) {
        fprintf(stderr, "mysql_store_result failed: %s\n", mysql_error(conn->conn));
        *msg = "DB error";
        return NULL;
    }
    cJSON* signup_req_list = cJSON_CreateArray();
    while ((row = mysql_fetch_row(signup_req_query_result))) {
        cJSON* signup_req_obj = cJSON_CreateObject();
        va_start(VA_LIST, key_num);
        for (int i = 0; i < key_num; i++)
        {
            const char* key = va_arg(VA_LIST, const char*);
            cJSON_AddStringToObject(signup_req_obj, key, row[i]);
        }
        va_end(VA_LIST);
        cJSON_AddItemToArray(signup_req_list, signup_req_obj);
    }
    mysql_free_result(signup_req_query_result);
    return signup_req_list;
}

bool init_mariadb(chatdb_t* db)
{
    db->db_names[USER_SETTING_DB_IDX] = USER_SETTING_DB;
    db->db_names[CHAT_GROUP_DB_IDX] = CHAT_GROUP_DB;
    db->db_names[STATISTIC_DB_IDX] = STATISTIC_DB;
    //db->db_names[USER_REQUEST_DB_IDX] = USER_REQUEST_DB;
    db->db_names[LOG_DB_IDX] = LOG_DB;;

    db->db_sizes[USER_SETTING_DB_IDX] = 8; // 모든 대상
    db->db_sizes[CHAT_GROUP_DB_IDX] = 8; // 모든 대상
    db->db_sizes[STATISTIC_DB_IDX] = 2; // 관리자
    //db->db_sizes[USER_REQUEST_DB_IDX] = 2; // 유저 + 관리자
    db->db_sizes[LOG_DB_IDX] = 2; // 관리자
    for (int i = 0; i < TOTAL_DB_NUM; i++)
    {
        printf("%s init\n", db->db_names[i]);
        if(init_mariadb_pool(&db->pools[i], db->db_sizes[i], i, db->db_names[i]) == false)
        {
            for (int j = 0; j < i; j++)
            {
                close_mariadb_pool(&db->pools[j]);
            }
            return false;
        }
        //printf("%s init done : %d - %d\n", db->db_names[i], db->pools[i].pool_idx_stack_top, db->pools[i].pool_idx_stack[db->pools[i].pool_idx_stack_top]);
    }
    return true;
}

void close_mariadb(chatdb_t* db)
{
    for (int i = 0; i < TOTAL_DB_NUM; i++)
    {
        close_mariadb_pool(&db->pools[i]);
    }
}