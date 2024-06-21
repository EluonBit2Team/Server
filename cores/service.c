#include "NetCore.h"

// ✨ 서비스 함수. 이런 형태의 함수들을 추가하여 서비스 추가. ✨
void echo_service(epoll_net_core* server_ptr, task_t* task) {
    printf("echo_service\n");
    client_session_t* now_session = find_session_by_fd(&server_ptr->session_pool, task->req_client_fd);
    if (now_session == NULL)
    {
        printf("invalid fd:%d", task->req_client_fd);
        return ;
    }
    reserve_send(&now_session->send_bufs, task->buf, task->task_data_len);
    
    struct epoll_event temp_event;
    temp_event.events = EPOLLOUT | EPOLLET;
    temp_event.data.fd = now_session->fd;
    if (epoll_ctl(server_ptr->epoll_fd, EPOLL_CTL_MOD, now_session->fd, &temp_event) == -1) {
        perror("epoll_ctl: add");
        close(task->req_client_fd);
    }
}

void login_service(epoll_net_core* server_ptr, task_t* task) {
    printf("login_service\n");
    int type = 100;
    char* msg = NULL;
    cJSON* result_json = cJSON_CreateObject();
    client_session_t* now_session = NULL;
    conn_t* user_setting_conn = NULL;
    conn_t* log_conn = NULL;
    char SQL_buf[512];
    int uid = -1;
    
    user_setting_conn = get_conn(&server_ptr->db.pools[USER_SETTING_DB_IDX]);
    log_conn = get_conn(&server_ptr->db.pools[LOG_DB_IDX]);

    now_session = find_session_by_fd(&server_ptr->session_pool, task->req_client_fd);
    if (now_session == NULL)
    {
        msg = "session error";
        goto cleanup_and_respond;
    }

    cJSON* json_ptr = get_parsed_json(task->buf);
    if (json_ptr == NULL) {
        msg = "user send invalid json";
        goto cleanup_and_respond;
    }
    cJSON* id_ptr = cJSON_GetObjectItem(json_ptr, "id");
    if (id_ptr == NULL) {
        msg = "user send invalid json. Miss name";
        goto cleanup_and_respond;
    }
    cJSON* pw_ptr = cJSON_GetObjectItem(json_ptr, "pw");
    if (pw_ptr == NULL) {
        msg = "user send invalid json. Miss pw";
        goto cleanup_and_respond;
    }

    snprintf(SQL_buf, sizeof(SQL_buf), 
        "SELECT uid FROM user WHERE '%s' = user.login_id AND UNHEX(SHA2('%s', %d)) = user.password",
        cJSON_GetStringValue(id_ptr), cJSON_GetStringValue(pw_ptr), SHA2_HASH_LENGTH);

    uid = query_result_to_int(user_setting_conn,&msg,SQL_buf);
    if (uid == false) {
        msg = "Invalid ID or PW";
        goto cleanup_and_respond;
    }
    
    insert(&server_ptr->fd_to_uid_hash, task->req_client_fd, uid);
    insert(&server_ptr->uid_to_fd_hash, uid, task->req_client_fd);

    printf("%d user login\n", find(&server_ptr->fd_to_uid_hash, task->req_client_fd));
    type = 2;
    
    // 로그인 성공시 DB에 로그 저장
    snprintf(SQL_buf, sizeof(SQL_buf), 
        "INSERT INTO client_log (uid, login_time) VALUES (%d, NOW())", uid);
    if (mysql_query(log_conn->conn, SQL_buf)) {
        fprintf(stderr, "SELECT failed: %s\n", mysql_error(user_setting_conn->conn));
        msg = "DB error";
        goto cleanup_and_respond;
    }

cleanup_and_respond:
    cJSON_AddNumberToObject(result_json, "type", type);
    cJSON_AddStringToObject(result_json, "id", cJSON_GetStringValue(id_ptr));
    if (msg != NULL)
    {
        cJSON_AddStringToObject(result_json, "msg", msg);
    }

    char *response_str = cJSON_Print(result_json);
    reserve_epoll_send(server_ptr->epoll_fd, now_session, response_str, strlen(response_str));
    release_conns(&server_ptr->db, 2, user_setting_conn, log_conn);
    cJSON_Delete(json_ptr);
    cJSON_Delete(result_json);
    return ;
}

void signup_service(epoll_net_core* server_ptr, task_t* task) {
    printf("signup_service\n");
    int type = 100;
    char* msg = NULL;
    cJSON* result_json = cJSON_CreateObject();
    client_session_t* now_session = NULL;
    conn_t* user_setting_conn = NULL;
    char SQL_buf[1024];

    user_setting_conn = get_conn(&server_ptr->db.pools[USER_SETTING_DB_IDX]);

    now_session = find_session_by_fd(&server_ptr->session_pool, task->req_client_fd);
    if (now_session == NULL) {
        printf("now_session NULL\n");
        goto cleanup_and_respond;
    }

    cJSON* json_ptr = get_parsed_json(task->buf);
    if (json_ptr == NULL) {
        msg = "user send invalid json";
        goto cleanup_and_respond;
    }
    cJSON* name_ptr = cJSON_GetObjectItem(json_ptr, "name");
    if (name_ptr == NULL || cJSON_GetStringValue(name_ptr)[0] == '\0') {
        msg = "user send invalid json. Miss name";
        goto cleanup_and_respond;
    }
    cJSON* id_ptr = cJSON_GetObjectItem(json_ptr, "id");
    if (id_ptr == NULL || cJSON_GetStringValue(id_ptr)[0] == '\0') {
        msg = "user send invalid json. Miss id";
        goto cleanup_and_respond;
    }
    cJSON* pw_ptr = cJSON_GetObjectItem(json_ptr, "pw");
    if (pw_ptr == NULL || cJSON_GetStringValue(pw_ptr)[0] == '\0') {
        msg = "user send invalid json. Miss pw";
        goto cleanup_and_respond;
    }
    cJSON* phone_ptr = cJSON_GetObjectItem(json_ptr, "phone");
    if (phone_ptr == NULL || cJSON_GetStringValue(phone_ptr)[0] == '\0') {
        msg = "user send invalid json. Miss phone";
        goto cleanup_and_respond;
    }
    cJSON* email_ptr = cJSON_GetObjectItem(json_ptr, "email");
    if (email_ptr == NULL || cJSON_GetStringValue(email_ptr)[0] == '\0') {
        msg = "user send invalid json. Miss email";
        goto cleanup_and_respond;
    }

    snprintf(SQL_buf, sizeof(SQL_buf), "SELECT COUNT(login_id) FROM user WHERE login_id = '%s'", cJSON_GetStringValue(id_ptr));
    if (query_result_to_int(user_setting_conn, &msg, SQL_buf) > 0) {
        msg = "login_id already exists.";
        goto cleanup_and_respond;
    }

    snprintf(SQL_buf, sizeof(SQL_buf), 
             "INSERT INTO signup_req (login_id, password, name, phone, email) VALUES ('%s', UNHEX(SHA2('%s',%d)), '%s', '%s', '%s')",
             cJSON_GetStringValue(id_ptr), cJSON_GetStringValue(pw_ptr), SHA2_HASH_LENGTH, cJSON_GetStringValue(name_ptr),
             cJSON_GetStringValue(phone_ptr), cJSON_GetStringValue(email_ptr));

    query_result_to_execuete(user_setting_conn, &msg, SQL_buf);
    if (msg != NULL) {
        goto cleanup_and_respond;
    }

    type = 1;

cleanup_and_respond:
    cJSON_AddNumberToObject(result_json, "type", type);
    if (msg != NULL) {
        cJSON_AddStringToObject(result_json, "msg", msg);
    }
    char *response_str = cJSON_Print(result_json);
    reserve_epoll_send(server_ptr->epoll_fd, now_session, response_str, strlen(response_str));
    release_conns(&server_ptr->db, 1, user_setting_conn);
    cJSON_Delete(json_ptr);
    cJSON_Delete(result_json);
    return ;
}

void make_group_service(epoll_net_core* server_ptr, task_t* task)
{
    printf("make_group_service\n");
    int type = 100;
    char* msg = NULL;
    cJSON* result_json = cJSON_CreateObject();
    client_session_t* now_session = NULL;
    conn_t* chat_group_conn = NULL;
    char SQL_buf[512];

    chat_group_conn = get_conn(&server_ptr->db.pools[CHAT_GROUP_DB_IDX]);

    now_session = find_session_by_fd(&server_ptr->session_pool, task->req_client_fd);
    if (now_session == NULL)
    {
        msg = "session error";
        goto cleanup_and_respond;
    }

    cJSON* json_ptr = get_parsed_json(task->buf);
    if (json_ptr == NULL)
    {
        msg = "user send invalid json";
        goto cleanup_and_respond;
    }

    cJSON* groupname_ptr = cJSON_GetObjectItem(json_ptr, "groupname");
    if (groupname_ptr == NULL || cJSON_GetStringValue(groupname_ptr)[0] == '\0')
    {
        msg = "user send invalid json. Miss groupname_ptr";
        goto cleanup_and_respond;
    }

    cJSON* id_ptr = cJSON_GetObjectItem(json_ptr, "id");
    if (id_ptr == NULL || cJSON_GetStringValue(id_ptr)[0] == '\0')
    {
        msg = "user send invalid json. Miss uid";
        goto cleanup_and_respond;
    }

    cJSON* message_ptr = cJSON_GetObjectItem(json_ptr, "message");
    if (message_ptr == NULL || cJSON_GetStringValue(message_ptr)[0] == '\0')
    {
        msg = "user send invalid json. Miss message";
        goto cleanup_and_respond;
    }

    int uid = find(&server_ptr->fd_to_uid_hash, task->req_client_fd);
    snprintf(SQL_buf, sizeof(SQL_buf), 
        "INSERT INTO group_req (groupname, uid, memo) VALUES ('%s', '%d','%s')",
        cJSON_GetStringValue(groupname_ptr), uid, cJSON_GetStringValue(message_ptr));
    
    query_result_to_execuete(chat_group_conn, &msg, SQL_buf);
    if (msg != NULL) {
        goto cleanup_and_respond;
    }
    type = 4;

cleanup_and_respond:
    cJSON_AddNumberToObject(result_json, "type", type);
    if (msg != NULL)
    {
        cJSON_AddStringToObject(result_json, "msg", msg);
    }
    char *response_str = cJSON_Print(result_json);
    reserve_epoll_send(server_ptr->epoll_fd, now_session, response_str, strlen(response_str));
    release_conns(&server_ptr->db, 1, chat_group_conn);
    cJSON_Delete(json_ptr);
    cJSON_Delete(result_json);
    return ;
}

void user_list_service(epoll_net_core* server_ptr, task_t* task) {
    printf("user_list_service\n");
    int type = 100;
    char* msg = NULL;
    cJSON* result_json = cJSON_CreateObject();
    client_session_t* now_session = NULL;
    conn_t* user_setting_conn = NULL;
    char SQL_buf[1024];

    user_setting_conn = get_conn(&server_ptr->db.pools[USER_SETTING_DB_IDX]);

    now_session = find_session_by_fd(&server_ptr->session_pool, task->req_client_fd);
    if (now_session == NULL) {
        msg = "session error";
        goto cleanup_and_respond;
    }

    cJSON* json_ptr = get_parsed_json(task->buf);
    if (json_ptr == NULL) {
        msg = "user send invalid json";
        goto cleanup_and_respond;
    }

    cJSON* page_ptr = cJSON_GetObjectItem(json_ptr, "page");
    if (page_ptr == NULL || !cJSON_IsNumber(page_ptr)) {
        msg = "user send invalid json. Miss page";
        goto cleanup_and_respond;
    }

    int page = cJSON_GetNumberValue(page_ptr);
    int limit = 10;
    int offset = (page - 1) * limit;

    snprintf(SQL_buf, sizeof(SQL_buf), 
        "SELECT u.login_id, u.name, jp.position_name, d.dept_name FROM user u LEFT JOIN dept d ON u.did = d.did LEFT JOIN job_position jp ON jp.pid = u.position LIMIT %d OFFSET %d", limit, offset);

    cJSON* user_list = query_result_to_json(user_setting_conn,&msg,SQL_buf,4,"login_id","name","position_name","dept_name");
    if (msg != NULL) {
        goto cleanup_and_respond;
    }
    type = 5;

cleanup_and_respond:
    cJSON_AddNumberToObject(result_json, "type", type);
    if (msg != NULL) {
        cJSON_AddStringToObject(result_json, "msg", msg);
    }
    else {
        cJSON_AddItemToObject(result_json, "users", user_list);
    }
    char *response_str = cJSON_Print(result_json);
    reserve_epoll_send(server_ptr->epoll_fd, now_session, response_str, strlen(response_str));
    release_conns(&server_ptr->db, 1, user_setting_conn);
    cJSON_Delete(json_ptr);
    cJSON_Delete(result_json);
    return ;
}

void group_list_service(epoll_net_core* server_ptr, task_t* task) {
    printf("group_list_service\n");
    int type = 100;
    const char* msg = NULL;
    cJSON* result_json = cJSON_CreateObject();
    client_session_t* now_session = NULL;
    conn_t* conn = NULL;
    MYSQL_RES *query_result = NULL;
    MYSQL_ROW row;
    char SQL_buf[512];

    cJSON* json_ptr = get_parsed_json(task->buf);
    if (json_ptr == NULL)
    {
        msg = "user send invalid json";
        goto cleanup_and_respond;
    }

    struct epoll_event temp_send_event;
    now_session = find_session_by_fd(&server_ptr->session_pool, task->req_client_fd);
    if (now_session == NULL)
    {
        msg = "session error";
        goto cleanup_and_respond;
    }
    temp_send_event.events = EPOLLOUT | EPOLLET;
    temp_send_event.data.fd = now_session->fd;

    int uid = find(&server_ptr->fd_to_uid_hash, task->req_client_fd);
    if (uid < 0)
    {
        msg = "login error";
        goto cleanup_and_respond;
    }

    snprintf(SQL_buf, sizeof(SQL_buf), "SELECT cg.groupname FROM group_member AS gm LEFT JOIN chat_group AS cg ON (gm.gid = cg.gid) WHERE gm.uid = %d", uid);
    conn = get_conn(&server_ptr->db.pools[CHAT_GROUP_DB_IDX]);
    if (mysql_query(conn->conn, SQL_buf)) {
        fprintf(stderr, "query fail: %s\n", mysql_error(conn->conn));
        msg = "DB error";
        goto cleanup_and_respond;
    }

    query_result = mysql_store_result(conn->conn);
    if (query_result == NULL) {
        fprintf(stderr, "mysql_store_result failed: %s\n", mysql_error(conn->conn));
        msg = "DB error";
        goto cleanup_and_respond;
    }
    
    cJSON* group_list = cJSON_CreateArray();
    while ((row = mysql_fetch_row(query_result))) {
        cJSON_AddItemToArray(group_list, cJSON_CreateString(row[0]));
    }

    type = 6;

cleanup_and_respond:
    printf("%d %s", task->req_client_fd, msg);
    cJSON_AddNumberToObject(result_json, "type", type);
    if (msg != NULL)
    {
        cJSON_AddStringToObject(result_json, "msg", msg);
    }
    else
    {
        cJSON_AddItemToObject(result_json, "groups", group_list);
    }
    char *response_str = cJSON_Print(result_json);
    reserve_send(&now_session->send_bufs, response_str, strlen(response_str));
    if (epoll_ctl(server_ptr->epoll_fd, EPOLL_CTL_MOD, now_session->fd, &temp_send_event) == -1) {
        perror("epoll_ctl: add");
    }
    if (conn != NULL)
    {
        release_conn(&server_ptr->db.pools[CHAT_GROUP_DB_IDX], conn);
    }
    if (query_result != NULL)
    {
        mysql_free_result(query_result);
    }
    cJSON_Delete(json_ptr);
    cJSON_Delete(result_json);
    return ;
}

void add_member_service(epoll_net_core* server_ptr, task_t* task) {
    printf("add_member_service\n");
    int type = 100;
    char* msg = NULL;
    cJSON* result_json = cJSON_CreateObject();
    client_session_t* now_session = NULL;
    conn_t* chat_group_conn = NULL;
    conn_t* user_setting_conn = NULL;
    char SQL_buf[512];
    
    chat_group_conn = get_conn(&server_ptr->db.pools[CHAT_GROUP_DB_IDX]);
    user_setting_conn = get_conn(&server_ptr->db.pools[USER_SETTING_DB_IDX]);
    
    int host_uid = find(&server_ptr->fd_to_uid_hash, task->req_client_fd);

    now_session = find_session_by_fd(&server_ptr->session_pool, task->req_client_fd);
    if (now_session == NULL)
    {
        msg = "session error";
        goto cleanup_and_respond;
    }

    cJSON* json_ptr = get_parsed_json(task->buf);
    if (json_ptr == NULL)
    {
        msg = "user send invalid json";
        goto cleanup_and_respond;
    }

    cJSON* groupname_ptr = cJSON_GetObjectItem(json_ptr, "groupname");
    if (groupname_ptr == NULL || cJSON_GetStringValue(groupname_ptr)[0] == '\0')
    {
        msg = "user send invalid json. Miss groupname_ptr";
        goto cleanup_and_respond;
    }

    cJSON* username_ptr = cJSON_GetObjectItem(json_ptr, "username");
    if (username_ptr == NULL || !cJSON_IsArray(username_ptr))
    {
        msg = "user send invalid json. Miss uid";
        goto cleanup_and_respond;
    }

    snprintf(SQL_buf, sizeof(SQL_buf), 
        "SELECT is_host FROM group_member WHERE uid = %d",
        host_uid);
    if (!query_result_to_int(chat_group_conn,&msg,SQL_buf)) {
        msg = "You are not host!!!!";
        goto cleanup_and_respond;
    }

    snprintf(SQL_buf, sizeof(SQL_buf), 
        "SELECT gid FROM group WHERE groupname = '%s'", cJSON_GetStringValue(groupname_ptr));

    int gid = query_result_to_int(chat_group_conn,&msg,SQL_buf);

    int array_size = cJSON_GetArraySize(username_ptr);
    for (int i = 0; i < array_size; i++) {
        cJSON* user_item = cJSON_GetArrayItem(username_ptr, i);
        if (user_item == NULL || cJSON_GetStringValue(user_item)[0] == '\0') {
            msg = "Invalid JSON: Empty username in users list";
            goto cleanup_and_respond;
        }

        snprintf(SQL_buf, sizeof(SQL_buf), "SELECT uid FROM user WHERE username = '%s'", cJSON_GetStringValue(user_item));
        int uid = query_result_to_int(user_setting_conn,&msg,SQL_buf);

        snprintf(SQL_buf, sizeof(SQL_buf), "INSERT INTO group_member (uid, gid) VALUES ('%d', '%d')", uid, gid);
        query_result_to_execuete(chat_group_conn,&msg,SQL_buf);
    }
    type = 7;
    goto cleanup_and_respond;

cleanup_and_respond:
    cJSON_AddNumberToObject(result_json, "type", type);
    if (msg != NULL) {
        cJSON_AddStringToObject(result_json, "msg", msg);
    }
    char *response_str = cJSON_Print(result_json);
    reserve_epoll_send(server_ptr->epoll_fd, now_session, response_str, strlen(response_str));
    release_conns(&server_ptr->db, 2, user_setting_conn, chat_group_conn);
    cJSON_Delete(json_ptr);
    cJSON_Delete(result_json);
    return ;
}

void Mng_req_list_servce(epoll_net_core* server_ptr, task_t* task) {
    printf("Mng_req_list_servce\n");
    int type = 100;
    char* msg = NULL;
    cJSON* result_json = cJSON_CreateObject();
    client_session_t* now_session = NULL;
    conn_t* user_setting_conn = NULL;
    conn_t* chat_group_conn = NULL;
    MYSQL_ROW row;
    char SQL_buf[512];

    cJSON* json_ptr = get_parsed_json(task->buf);
    if (json_ptr == NULL)
    {
        msg = "user send invalid json";
        goto cleanup_and_respond;
    }

    now_session = find_session_by_fd(&server_ptr->session_pool, task->req_client_fd);
    if (now_session == NULL)
    {
        msg = "session error";
        goto cleanup_and_respond;
    }

    int uid = find(&server_ptr->fd_to_uid_hash, task->req_client_fd);
    if (uid < 0)
    {
        msg = "login error";
        goto cleanup_and_respond;
    }
    user_setting_conn = get_conn(&server_ptr->db.pools[USER_SETTING_DB_IDX]);
    chat_group_conn = get_conn((&server_ptr->db.pools[CHAT_GROUP_DB_IDX]));

    // 유저 권한 확인 
    snprintf(SQL_buf, sizeof(SQL_buf), "SELECT user.uid FROM user WHERE user.uid = %d AND user.role = 1", uid);
    query_result_to_bool(user_setting_conn, &msg, SQL_buf);
    if (msg != NULL) {
        goto cleanup_and_respond;
    }

    // 유저 요청 리스트
    snprintf(SQL_buf, sizeof(SQL_buf), "SELECT login_id, name, phone, email FROM signup_req");
    cJSON* signup_req_list = query_result_to_json(user_setting_conn, &msg, SQL_buf, 4, "login_id", "name", "phone", "email");
    if (msg != NULL) {
        goto cleanup_and_respond;
    }

    // 그룹 요청 리스트 group_req_query_result
    snprintf(SQL_buf, sizeof(SQL_buf), "SELECT groupname, memo FROM group_req");
    cJSON* group_req_list = query_result_to_json(chat_group_conn, &msg, SQL_buf, 2, "group_name", "memo");
    if (msg != NULL) {
        goto cleanup_and_respond;
    }

    type = 8;


cleanup_and_respond:
    cJSON_AddNumberToObject(result_json, "type", type);
    if (msg != NULL)
    {
        cJSON_AddStringToObject(result_json, "msg", msg);
    }
    else
    {
        cJSON_AddItemToObject(result_json, "signup_req_list", signup_req_list);
        cJSON_AddItemToObject(result_json, "group_req_list", group_req_list);
        // signup_req_list, group_req_list 자동 삭제 됨??
    }
    char *response_str = cJSON_Print(result_json);
    reserve_epoll_send(server_ptr->epoll_fd, now_session, response_str, strlen(response_str));
    release_conns(&server_ptr->db, 2, user_setting_conn, chat_group_conn);
    cJSON_Delete(json_ptr);
    cJSON_Delete(result_json);
    return ;
}

void group_member_service(epoll_net_core* server_ptr, task_t* task) {
    printf("group_member_service\n");
    int type = 100;
    char* msg = NULL;
    cJSON* result_json = cJSON_CreateObject();
    client_session_t* now_session = NULL;
    conn_t* user_setting_conn = NULL;
    conn_t* chat_group_conn = NULL;
    char SQL_buf[512];

    user_setting_conn = get_conn(&server_ptr->db.pools[CHAT_GROUP_DB_IDX]);
    chat_group_conn = get_conn(&server_ptr->db.pools[USER_SETTING_DB_IDX]);

    cJSON* json_ptr = get_parsed_json(task->buf);
    if (json_ptr == NULL)
    {
        msg = "user send invalid json";
        goto cleanup_and_respond;
    }

    cJSON* groupname_ptr = cJSON_GetObjectItem(json_ptr, "groupname");
    printf("%s",cJSON_Print(groupname_ptr));
    if (groupname_ptr == NULL || cJSON_GetStringValue(groupname_ptr)[0] == '\0')
    {
        msg = "user send invalid json. Miss page";
        goto cleanup_and_respond;
    }

    now_session = find_session_by_fd(&server_ptr->session_pool, task->req_client_fd);
    if (now_session == NULL)
    {
        msg = "session error";
        goto cleanup_and_respond;
    }

    snprintf(SQL_buf, sizeof(SQL_buf), 
        "SELECT uid FROM group_member gm JOIN chat_group cg ON cg.gid = gm.gid WHERE cg.groupname = '%s'",cJSON_GetStringValue(groupname_ptr));

    int uid = query_result_to_int(user_setting_conn, &msg, SQL_buf);

    snprintf(SQL_buf, sizeof(SQL_buf), 
            "SELECT u.login_id, u.name, jp.position_name, d.dept_name FROM user u "
            "LEFT JOIN dept d ON u.did = d.did "
            "LEFT JOIN job_position jp ON jp.pid = u.position "
            "WHERE u.uid = %d", 
            uid);
    cJSON* group_user_list = query_result_to_json(chat_group_conn,&msg,SQL_buf,4,"login_id","name","position_name","dept_name");
    type = 11;

cleanup_and_respond:
    cJSON_AddNumberToObject(result_json, "type", type);
    if (msg != NULL) {
        cJSON_AddStringToObject(result_json, "msg", msg);
    }
    else {
        cJSON_AddItemToObject(result_json, "users", group_user_list);
    }
    char *response_str = cJSON_Print(result_json);
    reserve_epoll_send(server_ptr->epoll_fd, now_session, response_str, strlen(response_str));
    release_conns(&server_ptr->db, 2, user_setting_conn, chat_group_conn);
    cJSON_Delete(json_ptr);
    cJSON_Delete(result_json);
    return ;
}