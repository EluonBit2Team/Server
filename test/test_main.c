#include <stdio.h>
#include <stdlib.h>
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

// gcc -o t_main.out test_main.c -L/usr/lib64/mysql -lmysqlclient -std=c99

void finish_with_error(MYSQL *con) {
    fprintf(stderr, "%s\n", mysql_error(con));
    mysql_close(con);
    exit(1);
}

int main() {
    MYSQL *con = mysql_init(NULL);

    if (con == NULL) {
        fprintf(stderr, "mysql_init() failed\n");
        exit(1);
    }

    if (mysql_real_connect(con, DB_IP, DB_USER_NAME, DB_USER_PASS, USER_REQUEST_DB, DB_PORT, NULL, 0) == NULL) {
        finish_with_error(con);
    }

    // if (mysql_query(con, "SELECT * FROM your_table")) {
    //     finish_with_error(con);
    // }

    char query[1024];
    snprintf(query, sizeof(query), "INSERT INTO sign_req (login_id, password, name, phone, email, deptno, position) VALUES ('1','2','3','4','5','6','7')");

    
    if (mysql_query(con,query)) {
        fprintf(stderr, "INSERT failed\n");
        mysql_close(con);
    }

    MYSQL_RES *result = mysql_store_result(con);

    if (result == NULL) {
        finish_with_error(con);
    }

    int num_fields = mysql_num_fields(result);

    MYSQL_ROW row;

    // C99 표준 사용하여 for 루프 내 변수 선언
    // while ((row = mysql_fetch_row(result))) {
    //     for (int i = 0; i < num_fields; i++) {
    //         printf("%s ", row[i] ? row[i] : "NULL");
    //     }
    //     printf("\n");
    // }

    mysql_free_result(result);
    mysql_close(con);

    return 0;
}