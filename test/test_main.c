#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <mysql/mysql.h>

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
#define TEXT_LENGTH 10  // 랜덤 문자열 길이

void generate_random_text(char *text, size_t length) {
    static const char charset[] = "abcdefghijklmnopqrstuvwxyz";
    for (size_t i = 0; i < length; i++) {
        int key = rand() % (int)(sizeof(charset) - 1);
        text[i] = charset[key];
    }
    text[length] = '\0';
}

char* get_current_time(char *buffer, size_t size) {
    time_t t;
    struct tm *tm_info;

    time(&t);
    tm_info = localtime(&t);
    strftime(buffer, size, "%Y-%m-%d %H:%M:%S", tm_info);

    return buffer;
}

void insert_user_dummy_data(MYSQL *con) {
    char query[1024];
    int i;
    char current_time[20];

    get_current_time(current_time, sizeof(current_time));

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
    char current_time[20];
    char text[TEXT_LENGTH + 1];

    get_current_time(current_time, sizeof(current_time));

    for (i = 1; i <= 100; i++) {
        generate_random_text(text, TEXT_LENGTH);

        snprintf(query, sizeof(query),
            "INSERT INTO massage_log (mid, uid, gid, text, timestamp) VALUES "
            "(%d, %d, %d, '%s', '%s')",
            i, 1 + rand() % 100, 1 + rand() % 3, text, current_time);

        if (mysql_query(con, query)) {
            fprintf(stderr, "INSERT failed: %s\n", mysql_error(con));
            return;
        }
    }

    printf("100 dummy records inserted successfully.\n");
}

MYSQL* setup_database_connection() {
    MYSQL *conn = mysql_init(NULL);

    if (conn == NULL) {
        fprintf(stderr, "mysql_init() failed\n");
        exit(1);
    }

    if (mysql_real_connect(conn, DB_IP, DB_USER_NAME, DB_USER_PASS, LOG_DB, DB_PORT, NULL, 0) == NULL) {
        fprintf(stderr, "Connection failed: %s\n", mysql_error(conn));
        mysql_close(conn);
        exit(1);
    }

    return conn;
}

int main() {
    MYSQL *conn = setup_database_connection();

    insert_chat_dummy_data(conn);

    mysql_close(conn);

    return EXIT_SUCCESS;
}