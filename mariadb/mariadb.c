#include "mariadb.h"

bool init_mariadb(chatdb_t* db)
{
    db->db_names[USER_SETTING_D_IDX] = USER_SETTING_DB;
    db->db_names[CHAT_GROUP_DB_IDX] = CHAT_GROUP_DB;
    db->db_names[STATISTIC_DB_IDX] = STATISTIC_DB;
    db->db_names[USER_REQUEST_DB_IDX] = USER_REQUEST_DB;
    db->db_names[LOG_DB_IDX] = LOG_DB;;

    db->db_sizes[USER_SETTING_D_IDX] = 8; // 모든 대상
    db->db_sizes[CHAT_GROUP_DB_IDX] = 8; // 모든 대상
    db->db_sizes[STATISTIC_DB_IDX] = 2; // 관리자
    db->db_sizes[USER_REQUEST_DB_IDX] = 2; // 유저 + 관리자
    db->db_sizes[LOG_DB_IDX] = 2; // 관리자
    for (int i = 0; i < TOTAL_DB_NUM; i++)
    {
        if(init_mariadb_pool(&db->pools[i], db->db_sizes[i], db->db_names[i]) == false)
        {
            for (int j = 0; j < i; j++)
            {
                close_mariadb_pool(&db->pools[j]);
            }
            return false;
        }
        printf("%s init done\n", db->db_names[i]);
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