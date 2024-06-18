#include <stdio.h>
#include <stdlib.h>
#include <mysql/mysql.h>
#include "../mariadb/mariadb.h"

#define DB_IP "192.168.0.253"
#define DB_PORT 3307
#define DB_USER_NAME "today_chicken"
#define DB_USER_PASS "1q2w3e4r"
#define DB_CONN_TIMEOUT_SEC 3

#define USER_SETTING_DB "user_setting_db"
#define CHAT_GROUP_DB "chat_group_db"
#define STATISTIC_DB "server_statistic_db"
#define USER_REQUEST_DB "user_request_db"
#define LOG_DB "log_db"

// gcc -o t_main.out test_main.c -L/usr/lib64/mysql -lmysqlclient -std=c99

void insert_user_dummy_data(MYSQL *con) {
    char query[1024];
    int i;
    time_t t;
    struct tm *tm_info;
    char current_time[20];
    srand(time(NULL));
    time(&t);
    tm_info = localtime(&t);
    strftime(current_time, sizeof(current_time), "%Y-%m-%d %H:%M:%S", tm_info);

    for (i = 1; i <= 25; i++) {
        snprintf(query, sizeof(query), 
            "INSERT INTO user (login_id, password, name, phone, email, did, position, role, last_login_date, create_date) VALUES "
            "('login_id%d', UNHEX(SHA2('password%d', 256)), 'name%d', 'phone%d', 'email%d', %d, %d, %d, '%s', '%s')",
            i, i, i, i, i, 1 + rand() % 3, 1 + rand() % 3, 1 + rand() % 3, current_time, current_time);

        if (mysql_query(con, query)) {
            fprintf(stderr, "INSERT failed: %s\n", mysql_error(con));
            return;
        }
    }

    printf("25 dummy records inserted successfully.\n");
}

void insert_chat_dummy_data(MYSQL *con) {
    char query[1024];
    int i;
    time_t t;
    struct tm *tm_info;
    char current_time[20];
    srand(time(NULL));
    time(&t);
    tm_info = localtime(&t);
    strftime(current_time, sizeof(current_time), "%Y-%m-%d %H:%M:%S", tm_info);

    for (i = 1; i <= 100; i++) {
        snprintf(query, sizeof(query), 
            "INSERT INTO user (mid, uid, gid, text, timestamp) VALUES "
            "('%d', '%d', '%d', '%s','%s')",
            1 + rand() % 100, 1 + rand() % , 1 + rand() % 3, , current_time);

        if (mysql_query(con, query)) {
            fprintf(stderr, "INSERT failed: %s\n", mysql_error(con));
            return;
        }
    }

    printf("100 dummy records inserted successfully.\n");
}

int main() {
    MYSQL *conn = mysql_init(NULL);

    if (conn == NULL) {
        fprintf(stderr, "mysql_init() failed\n");
        exit(1);
    }

    if (mysql_real_connect(conn, DB_IP, DB_USER_NAME, DB_USER_PASS, LOG_DB, DB_PORT, NULL, 0) == NULL) {
        fprintf(stderr, "login query fail: %s\n", mysql_error(conn));
    }
    insert_chat_dummy_data(conn);
    mysql_close(conn);

    return EXIT_SUCCESS;
}