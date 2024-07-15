#include "NetCore.h"

// ✨ 서비스 함수. 이런 형태의 함수들을 추가하여 서비스 추가. ✨
void echo_service(epoll_net_core* server_ptr, task_t* task) {
    //printf("echo_service\n");
    int type = 100;
    char* msg = NULL;
    client_session_t* now_session = NULL;
    cJSON* result_json = cJSON_CreateObject();
    char* response_str = NULL;

    now_session = find_session_by_fd(&server_ptr->session_pool, task->req_client_fd);
    if (now_session == NULL)
    {
        msg = "session error";
        goto cleanup_and_respond;
    }

    type = 0;

cleanup_and_respond:
    cJSON_AddNumberToObject(result_json, "type", type);
    if (msg != NULL)
    {
        cJSON_AddStringToObject(result_json, "msg", msg);
    }

    response_str = cJSON_Print(result_json);
    reserve_epoll_send(server_ptr->epoll_fd, now_session, response_str, strlen(response_str));
    free_all(1, response_str);
    return ;
}

void login_service(epoll_net_core* server_ptr, task_t* task) {
    //printf("login_service\n");
    int type = 100;
    char* msg = NULL;
    char *response_str = NULL;
    cJSON* result_json = cJSON_CreateObject();
    client_session_t* now_session = NULL;
    conn_t* user_setting_conn = NULL;
    conn_t* log_conn = NULL;
    char SQL_buf[512];
    int uid = -1;
    int role = -1;

    user_setting_conn = get_conn(&server_ptr->db.pools[USER_SETTING_DB_IDX]);
    log_conn = get_conn(&server_ptr->db.pools[LOG_DB_IDX]);
    //printf("user_setting_conn : %p / log_conn : %p\n", user_setting_conn->conn, log_conn->conn);

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
    
    cJSON* id_ptr = cJSON_GetObjectItem(json_ptr, "login_id");
    if (id_ptr == NULL) {
        msg = "user send invalid json. Miss name";
        goto cleanup_and_respond;
    }
    cJSON* pw_ptr = cJSON_GetObjectItem(json_ptr, "pw");
    if (pw_ptr == NULL) {
        msg = "user send invalid json. Miss pw";
        goto cleanup_and_respond;
    }

    JSON_guard(json_ptr,&msg);
    if (msg != NULL) {
        goto cleanup_and_respond;
    }

    snprintf(SQL_buf, sizeof(SQL_buf), 
        "SELECT uid FROM user WHERE '%s' = user.login_id AND UNHEX(SHA2('%s', %d)) = user.password",
        cJSON_GetStringValue(id_ptr), cJSON_GetStringValue(pw_ptr), SHA2_HASH_LENGTH);

    uid = query_result_to_int(user_setting_conn,&msg,SQL_buf);

    if (uid < 0 || msg != NULL) {
        if (strcmp(msg, "No result") == 0) {
            msg = "Invalid ID or PW";
        }
        goto cleanup_and_respond;
    }

    // 중복 로그인 방지
    int fd = find(&server_ptr->uid_to_fd_hash, uid);
    if (fd >= 0) {
        type = 102;
        msg = "This user has already logged in";
        goto cleanup_and_respond;
    }
    
    snprintf(SQL_buf, sizeof(SQL_buf), 
        "SELECT role FROM user WHERE login_id = '%s'", cJSON_GetStringValue(id_ptr));

    role = query_result_to_int(user_setting_conn,&msg,SQL_buf);
    if (role < 0 || msg != NULL) {
        if (strcmp(msg, "No result") == 0) {
            msg = "No role data";
        }
        goto cleanup_and_respond;
    }

    insert(&server_ptr->fd_to_uid_hash, task->req_client_fd, uid);
    insert(&server_ptr->uid_to_fd_hash, uid, task->req_client_fd);

    //printf("%d user login\n", find(&server_ptr->fd_to_uid_hash, task->req_client_fd));
    type = 2;
    
    // 로그인 성공시 DB에 로그 저장
    snprintf(SQL_buf, sizeof(SQL_buf), "INSERT INTO client_log (uid, login_time, login_id) VALUES (%d, NOW(), '%s')", uid, cJSON_GetStringValue(id_ptr));
    if (mysql_query(log_conn->conn, SQL_buf)) {
        fprintf(stderr, "SELECT failed: %s\n", mysql_error(user_setting_conn->conn));
        msg = "DB error";
        goto cleanup_and_respond;
    }

    // 로그인 notice
    user_status_change_notice(server_ptr, user_setting_conn);

cleanup_and_respond:
    cJSON_AddNumberToObject(result_json, "type", type);
    if (msg != NULL) {
        cJSON_AddStringToObject(result_json, "msg", msg);
    }
    else {
        cJSON_AddStringToObject(result_json, "login_id", cJSON_GetStringValue(id_ptr));
        cJSON_AddNumberToObject(result_json, "role", role);
    }

    response_str = cJSON_Print(result_json);
    reserve_epoll_send(server_ptr->epoll_fd, now_session, response_str, strlen(response_str));
    release_conns(&server_ptr->db, 2, user_setting_conn, log_conn);
    cJSON_del(2, result_json, json_ptr);
    free_all(1, response_str);
    return ;
}

void signup_service(epoll_net_core* server_ptr, task_t* task) {
    //printf("signup_service\n");
    int type = 100;
    char* msg = NULL;
    char *response_str = NULL;
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
        msg = "json parse fail";
        goto cleanup_and_respond;
    }

    JSON_guard(json_ptr,&msg);
    if(msg != NULL) {
        goto cleanup_and_respond;
    }

    cJSON* name_ptr = cJSON_GetObjectItem(json_ptr, "name");
    if (name_ptr == NULL || cJSON_GetStringValue(name_ptr)[0] == '\0') {
        msg = "user send invalid json. Miss name";
        goto cleanup_and_respond;
    }
    cJSON* id_ptr = cJSON_GetObjectItem(json_ptr, "login_id");
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

    if (!is_valid_login_id(cJSON_GetStringValue(id_ptr),&msg)) {
        goto cleanup_and_respond;
    }
    if (!is_valid_password(cJSON_GetStringValue(pw_ptr),&msg)) {
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
    response_str = cJSON_Print(result_json);
    reserve_epoll_send(server_ptr->epoll_fd, now_session, response_str, strlen(response_str));
    release_conns(&server_ptr->db, 1, user_setting_conn);
    cJSON_del(2, result_json, json_ptr);
    free_all(1, response_str);
    return ;
}

void make_group_service(epoll_net_core* server_ptr, task_t* task)
{
    //printf("make_group_service\n");
    int type = 100;
    char* msg = NULL;
    char *response_str = NULL;
    cJSON* result_json = cJSON_CreateObject();
    client_session_t* now_session = NULL;
    conn_t* chat_group_conn = NULL;
    char SQL_buf[512];
    int count_groupname = 0;

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

    JSON_guard(json_ptr,&msg);
    if(msg != NULL) {
        goto cleanup_and_respond;
    }

    cJSON* groupname_ptr = cJSON_GetObjectItem(json_ptr, "groupname");
    if (groupname_ptr == NULL || cJSON_GetStringValue(groupname_ptr)[0] == '\0')
    {
        msg = "user send invalid json. Miss groupname_ptr";
        goto cleanup_and_respond;
    }

    cJSON* id_ptr = cJSON_GetObjectItem(json_ptr, "login_id");
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

    snprintf(SQL_buf,sizeof(SQL_buf),"SELECT COUNT(groupname) FROM chat_group WHERE groupname = '%s'",cJSON_GetStringValue(groupname_ptr));
    count_groupname += query_result_to_int(chat_group_conn, &msg, SQL_buf);
    if (msg != NULL) {
        goto cleanup_and_respond;
    }

    snprintf(SQL_buf,sizeof(SQL_buf),"SELECT COUNT(groupname) FROM group_req WHERE groupname = '%s'",cJSON_GetStringValue(groupname_ptr));
    count_groupname += query_result_to_int(chat_group_conn, &msg, SQL_buf);
    if (msg != NULL) {
        goto cleanup_and_respond;
    }

    if (count_groupname >= 1) {
        msg = "groupname already exists";
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
    response_str = cJSON_Print(result_json);
    reserve_epoll_send(server_ptr->epoll_fd, now_session, response_str, strlen(response_str));
    release_conns(&server_ptr->db, 1, chat_group_conn);
    cJSON_del(2, result_json, json_ptr);
    free_all(1, response_str);
    return ;
}

void user_list_service(epoll_net_core* server_ptr, task_t* task) {
    //printf("user_list_service\n");
    int type = 100;
    char* msg = NULL;
    char *response_str = NULL;
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

    snprintf(SQL_buf, sizeof(SQL_buf), 
         "SELECT u.login_id, u.name, jp.position_name, d.dept_name FROM user u LEFT JOIN dept d ON u.did = d.did LEFT JOIN job_position jp ON jp.pid = u.position");

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
    response_str = cJSON_Print(result_json);
    reserve_epoll_send(server_ptr->epoll_fd, now_session, response_str, strlen(response_str));
    release_conns(&server_ptr->db, 1, user_setting_conn);
    cJSON_del(2, result_json, json_ptr);
    free_all(1, response_str);
    return ;
}

void group_list_service(epoll_net_core* server_ptr, task_t* task) {
    //printf("group_list_service\n");
    int type = 100;
    char* msg = NULL;
    char *response_str = NULL;
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

    int uid = find(&server_ptr->fd_to_uid_hash, task->req_client_fd);

    snprintf(SQL_buf, sizeof(SQL_buf), "SELECT cg.groupname FROM group_member AS gm LEFT JOIN chat_group AS cg ON (gm.gid = cg.gid) WHERE gm.uid = %d", uid);
    
    cJSON* groupname_result = query_result_to_json(chat_group_conn, &msg, SQL_buf, 1, "groupname");
    type = 6;

cleanup_and_respond:
    cJSON_AddNumberToObject(result_json, "type", type);
    if (msg != NULL) {
        cJSON_AddStringToObject(result_json, "msg", msg);
    }
    else {
        cJSON_AddItemToObject(result_json, "groups", groupname_result);
    }
    response_str = cJSON_Print(result_json);
    reserve_epoll_send(server_ptr->epoll_fd, now_session, response_str, strlen(response_str));
    release_conns(&server_ptr->db, 1, chat_group_conn);
    cJSON_del(2, result_json, json_ptr);
    free_all(1, response_str);
    return ;
}

void edit_group_member_service(epoll_net_core* server_ptr, task_t* task) {
    //printf("add_member_service\n");
    int type = 100;
    char* msg = NULL;
    char *response_str = NULL;
    cJSON* result_json = cJSON_CreateObject();
    client_session_t* now_session = NULL;
    conn_t* chat_group_conn = NULL;
    conn_t* user_setting_conn = NULL;
    char SQL_buf[512];
    int array_size = 0;
    int user_count = 0;

    chat_group_conn = get_conn(&server_ptr->db.pools[CHAT_GROUP_DB_IDX]);
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
    cJSON* groupname_ptr = cJSON_GetObjectItem(json_ptr, "groupname");
    if (groupname_ptr == NULL || cJSON_GetStringValue(groupname_ptr)[0] == '\0') {
        msg = "user send invalid json. Miss groupname_ptr";
        goto cleanup_and_respond;
    }
    cJSON* inmem_ptr = cJSON_GetObjectItem(json_ptr, "in_member");
    if (inmem_ptr == NULL || !cJSON_IsArray(inmem_ptr)) {
        msg = "user send invalid json. Miss uid";
        goto cleanup_and_respond;
    }
    cJSON* outmem_ptr = cJSON_GetObjectItem(json_ptr, "out_member");
    if (outmem_ptr == NULL || !cJSON_IsArray(outmem_ptr)) {
        msg = "user send invalid json. Miss uid";
        goto cleanup_and_respond;
    }

    if (mysql_autocommit(user_setting_conn->conn, 0)) {
        msg = "transaction fail";
        goto cleanup_and_respond;
    }

    snprintf(SQL_buf, sizeof(SQL_buf), 
        "SELECT gid FROM chat_group WHERE groupname = '%s'", cJSON_GetStringValue(groupname_ptr));

    int gid = query_result_to_int(chat_group_conn, &msg, SQL_buf);
    if (msg != NULL) {
        goto cleanup_and_respond;
    }

    array_size = cJSON_GetArraySize(inmem_ptr);
    for (int i = 0; i < array_size; i++) {
        cJSON* user_item = cJSON_GetArrayItem(inmem_ptr, i);
        if (cJSON_GetStringValue(user_item)[0] != '\0') {
            snprintf(SQL_buf, sizeof(SQL_buf), "SELECT uid FROM user WHERE login_id = '%s'", cJSON_GetStringValue(user_item));
            int uid = query_result_to_int(user_setting_conn,&msg,SQL_buf);
            if (msg != NULL) {
                goto cleanup_and_respond;
            }

            snprintf(SQL_buf, sizeof(SQL_buf), "SELECT count(*) FROM group_member WHERE gid = %d AND uid = %d", gid, uid);
            user_count = query_result_to_int(chat_group_conn,&msg,SQL_buf);
            if (user_count > 0) {
                msg = "user is allready exist";
                goto cleanup_and_respond;
            }

            snprintf(SQL_buf, sizeof(SQL_buf), "INSERT INTO group_member (uid, gid,is_host) VALUES ('%d', '%d',0)", uid, gid);
            query_result_to_execuete(chat_group_conn,&msg,SQL_buf);
            if (msg != NULL) {
                mysql_rollback(user_setting_conn->conn);
                goto cleanup_and_respond;
            }
        }
    }

    array_size = cJSON_GetArraySize(outmem_ptr);
    for (int i = 0; i < array_size; i++) {
        cJSON* user_item = cJSON_GetArrayItem(outmem_ptr, i);
        if (cJSON_GetStringValue(user_item)[0] != '\0') {
            snprintf(SQL_buf, sizeof(SQL_buf), "SELECT uid FROM user WHERE login_id = '%s'", cJSON_GetStringValue(user_item));
            int uid = query_result_to_int(user_setting_conn,&msg,SQL_buf);
            if (uid < 0 || msg != NULL) {
                goto cleanup_and_respond;
            }
            snprintf(SQL_buf, sizeof(SQL_buf), "DELETE FROM group_member WHERE uid = %d", uid);
            query_result_to_execuete(chat_group_conn,&msg,SQL_buf);
            if (msg != NULL) {
                mysql_rollback(user_setting_conn->conn);
                goto cleanup_and_respond;
            }
        }
    }

    type = 7;

cleanup_and_respond:
    cJSON_AddNumberToObject(result_json, "type", type);
    if (msg != NULL) {
        cJSON_AddStringToObject(result_json, "msg", msg);
    }
    mysql_commit(user_setting_conn->conn);
    response_str = cJSON_Print(result_json);
    reserve_epoll_send(server_ptr->epoll_fd, now_session, response_str, strlen(response_str));
    release_conns(&server_ptr->db, 2, user_setting_conn, chat_group_conn);
    cJSON_del(2, result_json, json_ptr);
    free_all(1, response_str);
    return ;
}

void mng_req_list_service(epoll_net_core* server_ptr, task_t* task) {
    //printf("Mng_req_list_servce\n");
    int type = 100;
    char* msg = NULL;
    char *response_str = NULL;
    cJSON* result_json = cJSON_CreateObject();
    client_session_t* now_session = NULL;
    conn_t* user_setting_conn = NULL;
    conn_t* chat_group_conn = NULL;
    char SQL_buf[512];

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
    response_str = cJSON_Print(result_json);
    reserve_epoll_send(server_ptr->epoll_fd, now_session, response_str, strlen(response_str));
    release_conns(&server_ptr->db, 2, user_setting_conn, chat_group_conn);
    cJSON_del(2, result_json, json_ptr);
    free_all(1, response_str);
    return ;
}

void mng_signup_approve_service(epoll_net_core* server_ptr, task_t* task) {
    //printf("mng_signup_approve_service\n");
    int type = 100;
    char* msg = NULL;
    char *response_str = NULL;
    cJSON* result_json = cJSON_CreateObject();
    client_session_t* now_session = NULL;
    conn_t* user_setting_conn = NULL;
    char SQL_buf[512];
    int count_login_id = 0;

    user_setting_conn = get_conn(&server_ptr->db.pools[USER_SETTING_DB_IDX]);

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

    cJSON* json_ptr = get_parsed_json(task->buf);
    if (json_ptr == NULL)
    {
        msg = "user send invalid json";
        goto cleanup_and_respond;
    }

    JSON_guard(json_ptr,&msg);
    if(msg != NULL) {
        goto cleanup_and_respond;
    }

    cJSON* approve_ptr = cJSON_GetObjectItem(json_ptr, "is_ok");
    if (approve_ptr == NULL || !cJSON_IsNumber(approve_ptr)) {
        msg = "user send invalid json. Miss is_ok";
        goto cleanup_and_respond;
    }
    cJSON* id_ptr = cJSON_GetObjectItem(json_ptr, "login_id");
    if (id_ptr == NULL || cJSON_GetStringValue(id_ptr)[0] == '\0') {
        msg = "user send invalid json. Miss id";
        goto cleanup_and_respond;
    }

    snprintf(SQL_buf,sizeof(SQL_buf),"SELECT COUNT(login_id) FROM user WHERE login_id = '%s'",cJSON_GetStringValue(id_ptr));
    count_login_id = query_result_to_int(user_setting_conn, &msg, SQL_buf);
    if (msg != NULL) {
        goto cleanup_and_respond;
    }
    if (count_login_id >= 1) {
        msg = "login_id is duplicated";
        goto cleanup_and_respond;
    }

    if (mysql_autocommit(user_setting_conn->conn, 0)) {
        msg = "transaction fail";
        goto cleanup_and_respond;
    }

    if (cJSON_GetNumberValue(approve_ptr) == 0) {
        snprintf(SQL_buf, sizeof(SQL_buf),"DELETE FROM signup_req WHERE login_id = '%s'",cJSON_GetStringValue(id_ptr));
        query_result_to_execuete(user_setting_conn, &msg, SQL_buf);
        if (msg != NULL) {
            mysql_rollback(user_setting_conn->conn);
            goto cleanup_and_respond;
        }
        type = 9;
        goto cleanup_and_respond;
    }
    
    cJSON* dept_ptr = cJSON_GetObjectItem(json_ptr, "dept");
    if (dept_ptr == NULL || !cJSON_IsNumber(dept_ptr)) {
        msg = "user send invalid json. Miss dept";
        goto cleanup_and_respond;
    }
    cJSON* pos_ptr = cJSON_GetObjectItem(json_ptr, "pos");
    if (pos_ptr == NULL || !cJSON_IsNumber(pos_ptr)) {
        msg = "user send invalid json. Miss pos";
        goto cleanup_and_respond;
    }
    cJSON* role_ptr = cJSON_GetObjectItem(json_ptr, "role");
    if (role_ptr == NULL || !cJSON_IsNumber(role_ptr)) {
        msg = "user send invalid json. Miss role";
        goto cleanup_and_respond;
    }
    cJSON* max_tps_ptr = cJSON_GetObjectItem(json_ptr, "max_tps");
    if (max_tps_ptr == NULL || !cJSON_IsNumber(max_tps_ptr)) {
        msg = "user send invalid json. Miss max_tps";
        goto cleanup_and_respond;
    }
    
    int dept = cJSON_GetNumberValue(dept_ptr);
    int pos = cJSON_GetNumberValue(pos_ptr);
    int role = cJSON_GetNumberValue(role_ptr);
    int max_tps = cJSON_GetNumberValue(max_tps_ptr);

    snprintf(SQL_buf, sizeof(SQL_buf), "SELECT uid FROM user WHERE uid = %d AND role = 1", uid);
    query_result_to_bool(user_setting_conn, &msg, SQL_buf);
    if (msg != NULL) {
        goto cleanup_and_respond;
    }

    snprintf(SQL_buf, sizeof(SQL_buf), "INSERT INTO user (login_id, password, name, phone, email, did, position, role, max_tps, create_date)\
                                        SELECT login_id, password, name, phone, email, %d, %d, %d, %d, NOW() FROM \
                                        signup_req WHERE login_id = '%s'", dept, pos, role, max_tps, cJSON_GetStringValue(id_ptr));
    query_result_to_execuete(user_setting_conn, &msg, SQL_buf);
    if (msg != NULL) {
        mysql_rollback(user_setting_conn->conn);
        goto cleanup_and_respond;
    }

    snprintf(SQL_buf, sizeof(SQL_buf),"DELETE FROM signup_req WHERE login_id = '%s'",cJSON_GetStringValue(id_ptr));
    query_result_to_execuete(user_setting_conn, &msg, SQL_buf);
    if (msg != NULL) {
        mysql_rollback(user_setting_conn->conn);
        goto cleanup_and_respond;
    }
    type = 9;


cleanup_and_respond:
    cJSON_AddNumberToObject(result_json, "type", type);
    if (msg != NULL)
    {
        cJSON_AddStringToObject(result_json, "msg", msg);
    }
    mysql_commit(user_setting_conn->conn);
    response_str = cJSON_Print(result_json);
    reserve_epoll_send(server_ptr->epoll_fd, now_session, response_str, strlen(response_str));
    release_conns(&server_ptr->db, 1, user_setting_conn);
    cJSON_del(2, result_json, json_ptr);
    free_all(1, response_str);
    return ;
}

void mng_group_approve_service(epoll_net_core* server_ptr, task_t* task) {
    //printf("mng_group_approve_service\n");
    int type = 100;
    char* msg = NULL;
    char *response_str = NULL;
    cJSON* result_json = cJSON_CreateObject();
    client_session_t* now_session = NULL;
    conn_t* chat_group_conn = NULL;
    char SQL_buf[512];
    int count_groupname = 0;

    chat_group_conn = get_conn(&server_ptr->db.pools[CHAT_GROUP_DB_IDX]);

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

    cJSON* json_ptr = get_parsed_json(task->buf);
    if (json_ptr == NULL)
    {
        msg = "user send invalid json";
        goto cleanup_and_respond;
    }

    JSON_guard(json_ptr,&msg);
    if(msg != NULL) {
        goto cleanup_and_respond;
    }

    cJSON* approve_ptr = cJSON_GetObjectItem(json_ptr, "is_ok");
    if (approve_ptr == NULL || !cJSON_IsNumber(approve_ptr)) {
        msg = "user send invalid json. Miss is_ok";
        goto cleanup_and_respond;
    }
    cJSON* groupname_ptr = cJSON_GetObjectItem(json_ptr, "groupname");
    if (groupname_ptr == NULL || !cJSON_GetStringValue(groupname_ptr)) {
        msg = "user send invalid json. Miss groupname";
        goto cleanup_and_respond;
    }

    snprintf(SQL_buf,sizeof(SQL_buf),"SELECT COUNT(groupname) FROM chat_group WHERE groupname = '%s'",cJSON_GetStringValue(groupname_ptr));
    count_groupname = query_result_to_int(chat_group_conn, &msg, SQL_buf);
    if (msg != NULL) {
        goto cleanup_and_respond;
    }
    if (count_groupname >= 1) {
        msg = "groupname is duplicated";
        goto cleanup_and_respond;
    }
    
    if (mysql_autocommit(chat_group_conn->conn, 0)) {
        msg = "transaction fail";
        goto cleanup_and_respond;
    }
  
    if (cJSON_GetNumberValue(approve_ptr) == 0) {
        snprintf(SQL_buf, sizeof(SQL_buf),"DELETE FROM group_req WHERE groupname = '%s'",cJSON_GetStringValue(groupname_ptr));
        query_result_to_execuete(chat_group_conn, &msg, SQL_buf);
        if (msg != NULL) {
            mysql_rollback(chat_group_conn->conn);
            printf("rollback\n");
            goto cleanup_and_respond;
        }
        type = 10;
        goto cleanup_and_respond;
    }

    snprintf(SQL_buf, sizeof(SQL_buf),"INSERT INTO chat_group (groupname) VALUES ('%s')",cJSON_GetStringValue(groupname_ptr));
    query_result_to_execuete(chat_group_conn, &msg, SQL_buf);
    if (msg != NULL) {
        mysql_rollback(chat_group_conn->conn);
        printf("rollback\n");
        goto cleanup_and_respond;
    }

    snprintf(SQL_buf, sizeof(SQL_buf),"SELECT uid FROM group_req WHERE groupname = '%s'",cJSON_GetStringValue(groupname_ptr));
    int uid_value = query_result_to_int(chat_group_conn, &msg, SQL_buf);
    if (msg != NULL) {
        mysql_rollback(chat_group_conn->conn);
        printf("rollback\n");
        goto cleanup_and_respond;
    }

    snprintf(SQL_buf, sizeof(SQL_buf),"SELECT gid FROM chat_group WHERE groupname = '%s'",cJSON_GetStringValue(groupname_ptr));
    int gid_value = query_result_to_int(chat_group_conn, &msg, SQL_buf);
    if (msg != NULL) {
        mysql_rollback(chat_group_conn->conn);
        printf("rollback\n");
        goto cleanup_and_respond;
    }

    snprintf(SQL_buf, sizeof(SQL_buf),"INSERT INTO group_member (uid, gid,is_host) VALUES ('%d','%d',1)",uid_value,gid_value);
    query_result_to_execuete(chat_group_conn, &msg, SQL_buf);
    if (msg != NULL) {
        mysql_rollback(chat_group_conn->conn);
        printf("rollback\n");
        goto cleanup_and_respond;
    }

    snprintf(SQL_buf, sizeof(SQL_buf),"DELETE FROM group_req WHERE groupname = '%s'",cJSON_GetStringValue(groupname_ptr));
    query_result_to_execuete(chat_group_conn, &msg, SQL_buf);
    if (msg != NULL) {
        mysql_rollback(chat_group_conn->conn);
        printf("rollback\n");
        goto cleanup_and_respond;
    }

    type = 10;


cleanup_and_respond:
    cJSON_AddNumberToObject(result_json, "type", type);
    if (msg != NULL)
    {
        cJSON_AddStringToObject(result_json, "msg", msg);
    }
    mysql_commit(chat_group_conn->conn);

    response_str = cJSON_Print(result_json);
    reserve_epoll_send(server_ptr->epoll_fd, now_session, response_str, strlen(response_str));
    release_conns(&server_ptr->db, 1, chat_group_conn);
    cJSON_del(2, result_json, json_ptr);
    free_all(1, response_str);
    return ;
}

void get_group_members_service(epoll_net_core* server_ptr, task_t* task) {
    //printf("get_group_members_service\n");
    int type = 100;
    char* msg = NULL;
    char *response_str = NULL;
    cJSON* result_json = cJSON_CreateObject();
    client_session_t* now_session = NULL;
    conn_t* user_setting_conn = NULL;
    conn_t* chat_group_conn = NULL;
    char SQL_buf[512];
    char uid_list_str[1024] = "";

    user_setting_conn = get_conn(&server_ptr->db.pools[CHAT_GROUP_DB_IDX]);
    chat_group_conn = get_conn(&server_ptr->db.pools[USER_SETTING_DB_IDX]);

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
        msg = "user send invalid json. Miss page";
        goto cleanup_and_respond;
    }

    snprintf(SQL_buf, sizeof(SQL_buf), 
        "SELECT uid FROM group_member gm JOIN chat_group cg ON cg.gid = gm.gid WHERE cg.groupname = '%s'",cJSON_GetStringValue(groupname_ptr));

    cJSON* uid_list = query_result_to_json(user_setting_conn, &msg, SQL_buf, 1, "uid");
    if (msg != NULL) {
        goto cleanup_and_respond;
    }

    int uid_count = cJSON_GetArraySize(uid_list);
    for (int i = 0; i < uid_count; i++) {
        cJSON* item = cJSON_GetArrayItem(uid_list, i);
        cJSON* uid_value = cJSON_GetObjectItemCaseSensitive(item, "uid");
        if (cJSON_IsString(uid_value) && (uid_value->valuestring != NULL)) {
            if (strlen(uid_list_str) > 0) {
            strcat(uid_list_str, ",");
        }
        strcat(uid_list_str, uid_value->valuestring);
        }
    }

    int query_len = snprintf(SQL_buf, sizeof(SQL_buf), 
            "SELECT u.login_id, u.name, jp.position_name, d.dept_name FROM user u \
             LEFT JOIN dept d ON u.did = d.did \
             LEFT JOIN job_position jp ON jp.pid = u.position \
             WHERE u.uid IN (%s)", 
            uid_list_str);
    if (query_len >= sizeof(SQL_buf)) {
        fprintf(stderr, "Query string is too long and was truncated.\n");
        msg = "buffer error";
        goto cleanup_and_respond;
    }

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
    response_str = cJSON_Print(result_json);
    reserve_epoll_send(server_ptr->epoll_fd, now_session, response_str, strlen(response_str));
    release_conns(&server_ptr->db, 2, user_setting_conn, chat_group_conn);
    cJSON_del(2, result_json, json_ptr);
    free_all(1, response_str);
    return ;
}

void chat_in_group_service(epoll_net_core* server_ptr, task_t* task) {
    //printf("chat_in_group_service\n");
    int type = 100;
    char* msg = NULL;
    cJSON* result_json = cJSON_CreateObject();
    client_session_t* now_session = NULL;
    conn_t* user_setting_conn = NULL;
    conn_t* chat_group_conn = NULL;
    conn_t* log_conn = NULL;
    char* response_str = NULL;
    char* timestamp = NULL;
    int* recieve_fd_array = NULL;
    char SQL_buf[1024];

    now_session = find_session_by_fd(&server_ptr->session_pool, task->req_client_fd);
    if (now_session == NULL) {
        msg = "Session Error";
        goto cleanup_and_respond;
    }

    cJSON* json_ptr = get_parsed_json(task->buf);
    if (json_ptr == NULL)
    {
        msg = "user send invalid json";
        goto cleanup_and_respond;
    }

    // 유저 로그인 확인
    int uid = find(&server_ptr->fd_to_uid_hash, task->req_client_fd);
    if (uid < 0) {
        msg = "Invalid user";
        goto cleanup_and_respond;
    }
    
    cJSON* groupname_ptr = cJSON_GetObjectItem(json_ptr, "groupname");
    if (groupname_ptr == NULL || cJSON_GetStringValue(groupname_ptr)[0] == '\0')
    {
        msg = "user send invalid json. Miss groupname";
        goto cleanup_and_respond;
    }

    cJSON* login_id_ptr = cJSON_GetObjectItem(json_ptr, "login_id");
    if (login_id_ptr == NULL || cJSON_GetStringValue(login_id_ptr)[0] == '\0')
    {
        msg = "user send invalid json. Miss login_id";
        goto cleanup_and_respond;
    }

    cJSON* text_ptr = cJSON_GetObjectItem(json_ptr, "text");
    if (text_ptr == NULL || cJSON_GetStringValue(text_ptr)[0] == '\0')
    {
        msg = "user send invalid json. Miss text";
        goto cleanup_and_respond;
    }
    
    user_setting_conn = get_conn(&server_ptr->db.pools[USER_SETTING_DB_IDX]);
    chat_group_conn = get_conn(&server_ptr->db.pools[CHAT_GROUP_DB_IDX]);
    log_conn = get_conn(&server_ptr->db.pools[LOG_DB_IDX]);
    
    // 해당 유저가 해당 그룹인지 확인 -> gid를 가져옴
    snprintf(SQL_buf, sizeof(SQL_buf), 
        "SELECT gm.gid FROM group_member AS gm LEFT JOIN chat_group AS cg ON cg.gid = gm.gid \
        WHERE cg.groupname = '%s' AND gm.uid = %d", cJSON_GetStringValue(groupname_ptr), uid);
    int gid = query_result_to_int(chat_group_conn, &msg, SQL_buf);
    if (msg != NULL) {
        goto cleanup_and_respond;
    }
    
    snprintf(SQL_buf, sizeof(SQL_buf), 
        "SELECT user.max_tps FROM user WHERE user.uid = %d", uid);
    int max_tps = query_result_to_int(user_setting_conn, &msg, SQL_buf);
    if (msg != NULL) {
        goto cleanup_and_respond;
    }
    
    snprintf(SQL_buf, sizeof(SQL_buf), 
        "CALL insert_message(%d, %d, '%s', %d, '%s','%s',@result)", 
        uid, gid, cJSON_GetStringValue(text_ptr), max_tps,cJSON_GetStringValue(login_id_ptr),cJSON_GetStringValue(groupname_ptr));
    query_result_to_execuete(log_conn, &msg, SQL_buf);
    if (msg != NULL) {
        goto cleanup_and_respond;
    }
    
    snprintf(SQL_buf, sizeof(SQL_buf), "SELECT @result");
    timestamp = query_result_to_str(log_conn, &msg, SQL_buf);
    if (msg != NULL) {
        goto cleanup_and_respond;
    }
    if (timestamp == NULL) {
        type = 101;
        msg = "Too Much Message in Minute";
        goto cleanup_and_respond;
    }
    
    snprintf(SQL_buf, sizeof(SQL_buf), "SELECT uid FROM group_member AS gm WHERE gm.gid = %d", gid);
    cJSON* uid_list = query_result_to_json(chat_group_conn, &msg, SQL_buf, 1, "uid");
    int uid_count = cJSON_GetArraySize(uid_list);
    recieve_fd_array = (int*)malloc(sizeof(int) * uid_count);
    // TODO: uid_list_str 동적 할당. 및 버퍼 크기 오버 예외 처리.
    for (int i = 0; i < uid_count; i++) {
        cJSON* item = cJSON_GetArrayItem(uid_list, i);
        cJSON* uid_value = cJSON_GetObjectItemCaseSensitive(item, "uid");
        if (cJSON_IsString(uid_value) && (uid_value->valuestring != NULL)) {
            int uid = atoi(uid_value->valuestring);
            if (uid == 0 && uid_value->valuestring[0] != '0') {
                msg = "invalid uid";
                goto cleanup_and_respond;
            }
            recieve_fd_array[i] = find(&server_ptr->uid_to_fd_hash, uid);
        }
    }

    // 받은거에서 timestamp붙여서 그대로 날려줌.
    cJSON_AddStringToObject(json_ptr, "timestamp", timestamp);
    response_str = cJSON_Print(json_ptr);
    for (int i = 0; i < uid_count; i++) {
        //printf("%d \n", recieve_fd_array[i]); write(STDOUT_FILENO, task->buf, task->task_data_len); write(STDOUT_FILENO, "\n", 1);
        if (recieve_fd_array[i] < 0) {
            continue;
        }
        client_session_t* session = find_session_by_fd(&server_ptr->session_pool, recieve_fd_array[i]);
        if (session == NULL) {
            printf("%d fd Not have session!!\n", recieve_fd_array[i]);
            continue;
        }
        // 그대로 echo때려버리면 될듯.
        //write(STDOUT_FILENO, "true:", 5); write(STDOUT_FILENO, task->buf + HEADER_SIZE, task->task_data_len - HEADER_SIZE); write(STDOUT_FILENO, "\n", 1);
        reserve_epoll_send(server_ptr->epoll_fd, session, response_str, strlen(response_str));
    }


cleanup_and_respond:
    if (msg != NULL) {
        cJSON_AddNumberToObject(result_json, "type", type);
        cJSON_AddStringToObject(result_json, "msg", msg);
        response_str = cJSON_Print(result_json);
        reserve_epoll_send(server_ptr->epoll_fd, now_session, response_str, strlen(response_str));
    }
    release_conns(&server_ptr->db, 3, log_conn, chat_group_conn, user_setting_conn);
    cJSON_del(2, json_ptr, result_json);
    free_all(2, response_str, timestamp);
    return ;
}

void mng_edit_user_info_service(epoll_net_core* server_ptr, task_t* task) {
    //printf("mng_edit_user_info_service\n");
    int type = 100;
    char* msg = NULL;
    char *response_str = NULL;
    cJSON* result_json = cJSON_CreateObject();
    client_session_t* now_session = NULL;
    conn_t* user_setting_conn = NULL;
    char SQL_buf[1024];

    user_setting_conn = get_conn(&server_ptr->db.pools[USER_SETTING_DB_IDX]); 

    now_session = find_session_by_fd(&server_ptr->session_pool, task->req_client_fd);
    if (now_session == NULL) {
        msg = "Session Error";
        goto cleanup_and_respond;
    }

    cJSON* json_ptr = get_parsed_json(task->buf);
    if (json_ptr == NULL) {
        msg = "user send invalid json";
        goto cleanup_and_respond;
    }
    cJSON* login_id_ptr = cJSON_GetObjectItem(json_ptr, "login_id");
    if (login_id_ptr == NULL || cJSON_GetStringValue(login_id_ptr)[0] == '\0') {
        msg = "user send invalid json. Miss login_id";
        goto cleanup_and_respond;
    }

    if (mysql_autocommit(user_setting_conn->conn, 0)) {
        msg = "transaction fail";
        goto cleanup_and_respond;
    }

    cJSON* name_ptr = cJSON_GetObjectItem(json_ptr, "name");
    if (name_ptr == NULL) {
        msg = "user send invalid json. Miss name";
        goto cleanup_and_respond;
    }
    else if (cJSON_GetStringValue(name_ptr)[0] != '\0') {
        snprintf(SQL_buf, sizeof(SQL_buf), "UPDATE user SET name = '%s' WHERE login_id = '%s'",cJSON_GetStringValue(name_ptr),cJSON_GetStringValue(login_id_ptr));
        query_result_to_execuete(user_setting_conn, &msg, SQL_buf);
        if (msg != NULL) {
            mysql_rollback(user_setting_conn->conn);
            goto cleanup_and_respond;
        }
    }
    cJSON* phone_ptr = cJSON_GetObjectItem(json_ptr, "phone");
    if (phone_ptr == NULL) {
        msg = "user send invalid json. Miss phone";
        goto cleanup_and_respond;
    }
    else if (cJSON_GetStringValue(phone_ptr)[0] != '\0') {
        snprintf(SQL_buf, sizeof(SQL_buf), "UPDATE user SET phone = '%s' WHERE login_id = '%s'",cJSON_GetStringValue(phone_ptr),cJSON_GetStringValue(login_id_ptr));
        query_result_to_execuete(user_setting_conn, &msg, SQL_buf);
        if (msg != NULL) {
            mysql_rollback(user_setting_conn->conn);
            goto cleanup_and_respond;
        }
    }

    cJSON* email_ptr = cJSON_GetObjectItem(json_ptr, "email");
    if (email_ptr == NULL) {
        msg = "user send invalid json. Miss email";
        goto cleanup_and_respond;
    }
    else if (cJSON_GetStringValue(email_ptr)[0] != '\0') {
        snprintf(SQL_buf, sizeof(SQL_buf), "UPDATE user SET email = '%s' WHERE login_id = '%s'",cJSON_GetStringValue(email_ptr),cJSON_GetStringValue(login_id_ptr));
        query_result_to_execuete(user_setting_conn, &msg, SQL_buf);
        if (msg != NULL) {
            mysql_rollback(user_setting_conn->conn);
            goto cleanup_and_respond;
        }
    }

    cJSON* dept_ptr = cJSON_GetObjectItem(json_ptr, "dept");
    if (dept_ptr == NULL) {
        msg = "user send invalid json. Miss dept";
        goto cleanup_and_respond;
    }
    int dept_value = cJSON_GetNumberValue(dept_ptr);
    if (dept_value != 999) {
        snprintf(SQL_buf, sizeof(SQL_buf), "UPDATE user SET did = %d WHERE login_id = '%s'",dept_value, cJSON_GetStringValue(login_id_ptr));
        query_result_to_execuete(user_setting_conn, &msg, SQL_buf);
        if (msg != NULL) {
            type = 13;
            mysql_rollback(user_setting_conn->conn);
            goto cleanup_and_respond;
        }
    }
    cJSON* pos_ptr = cJSON_GetObjectItem(json_ptr, "pos");
    if (pos_ptr == NULL) {
        msg = "user send invalid json. Miss pos";
        goto cleanup_and_respond;
    }
    int pos_value = cJSON_GetNumberValue(pos_ptr);
    if (cJSON_GetNumberValue(pos_ptr) != 999) {
        snprintf(SQL_buf, sizeof(SQL_buf), "UPDATE user SET position = %d WHERE login_id = '%s'",pos_value,cJSON_GetStringValue(login_id_ptr));
        query_result_to_execuete(user_setting_conn, &msg, SQL_buf);
        if (msg != NULL) {
            type = 13;
            mysql_rollback(user_setting_conn->conn);
            goto cleanup_and_respond;
        }
    }
    cJSON* role_ptr = cJSON_GetObjectItem(json_ptr, "role");
    if (role_ptr == NULL) {
        msg = "user send invalid json. Miss role";
        goto cleanup_and_respond;
    }
    int role_value = cJSON_GetNumberValue(role_ptr);
    if (cJSON_GetNumberValue(role_ptr) != 999) {
        snprintf(SQL_buf, sizeof(SQL_buf), "UPDATE user SET role = %d WHERE login_id = '%s'",role_value ,cJSON_GetStringValue(login_id_ptr));
        query_result_to_execuete(user_setting_conn, &msg, SQL_buf);
        if (msg != NULL) {
            type = 13;
            mysql_rollback(user_setting_conn->conn);
            goto cleanup_and_respond;
        }
    }
    cJSON* max_tps_ptr = cJSON_GetObjectItem(json_ptr, "max_tps");
    if (max_tps_ptr == NULL) {
        msg = "user send invalid json. Miss max_tps";
        goto cleanup_and_respond;
    }
    int max_tps_value = cJSON_GetNumberValue(max_tps_ptr);
    if (cJSON_GetNumberValue(max_tps_ptr) != 999) {
        snprintf(SQL_buf, sizeof(SQL_buf), "UPDATE user SET max_tps = %d WHERE login_id = '%s'",max_tps_value,cJSON_GetStringValue(login_id_ptr));
        query_result_to_execuete(user_setting_conn, &msg, SQL_buf);
        if (msg != NULL) {
            type = 13;
            mysql_rollback(user_setting_conn->conn);
            goto cleanup_and_respond;
        }
    }

    type = 13;

cleanup_and_respond:
    cJSON_AddNumberToObject(result_json, "type", type);
    if (msg != NULL) {
        cJSON_AddStringToObject(result_json, "msg", msg);
    }
    mysql_commit(user_setting_conn->conn);
    response_str = cJSON_Print(result_json);
    reserve_epoll_send(server_ptr->epoll_fd, now_session, response_str, strlen(response_str));
    release_conns(&server_ptr->db, 1, user_setting_conn);
    cJSON_del(2, result_json, json_ptr);
    free_all(1, response_str);
    return ;
}

void pre_chat_log_service(epoll_net_core* server_ptr, task_t* task) {
    printf("pre_chat_log_service\n");
    int type = 100;
    char* msg = NULL;
    char *response_str = NULL;
    cJSON* result_json = cJSON_CreateObject();
    client_session_t* now_session = NULL;
    conn_t* chat_group_conn = NULL;
    conn_t* log_conn = NULL;
    char SQL_buf[1024];
    char uid_list_str[1024] = "";
    int gid_value = 0;

    now_session = find_session_by_fd(&server_ptr->session_pool, task->req_client_fd);
    if (now_session == NULL) {
        msg = "Session Error";
        goto cleanup_and_respond;
    }

    chat_group_conn = get_conn(&server_ptr->db.pools[CHAT_GROUP_DB_IDX]);  
    log_conn = get_conn(&server_ptr->db.pools[LOG_DB_IDX]);  

    cJSON* json_ptr = get_parsed_json(task->buf);
    if (json_ptr == NULL) {
        msg = "user send invalid json";
        goto cleanup_and_respond;
    }

    cJSON* groupname_ptr = cJSON_GetObjectItem(json_ptr, "groupname");
    if (groupname_ptr == NULL || cJSON_GetStringValue(groupname_ptr)[0] == '\0') {
        msg = "user send invalid json. Miss groupname";
        goto cleanup_and_respond;
    }

    cJSON* start_time_ptr = cJSON_GetObjectItem(json_ptr, "start_time");
    if (start_time_ptr == NULL || cJSON_GetStringValue(start_time_ptr)[0] == '\0') {
        msg = "user send invalid json. Miss start_time";
        goto cleanup_and_respond;
    }
    cJSON* end_time_ptr = cJSON_GetObjectItem(json_ptr, "end_time");
    if (end_time_ptr == NULL || cJSON_GetStringValue(end_time_ptr)[0] == '\0') {
        msg = "user send invalid json. Miss end_time";
        goto cleanup_and_respond;
    }
    snprintf(SQL_buf, sizeof(SQL_buf), "SELECT gid FROM chat_group WHERE groupname = '%s'",cJSON_GetStringValue(groupname_ptr));
    gid_value = query_result_to_int(chat_group_conn, &msg, SQL_buf);
    if (msg != NULL) {
        goto cleanup_and_respond;
    }

    snprintf(SQL_buf, sizeof(SQL_buf), "SELECT * FROM ( \
    SELECT mid, login_id, text, timestamp FROM message_log \
    WHERE gid = %d AND timestamp BETWEEN '%s' AND '%s' \
    ORDER BY timestamp DESC LIMIT 500) AS subquery \
    ORDER BY mid ASC;",
    gid_value,cJSON_GetStringValue(start_time_ptr),cJSON_GetStringValue(end_time_ptr));
    
    printf("buf : %s",SQL_buf);
    printf("\n");
    cJSON* chat_log = query_result_to_json(log_conn, &msg, SQL_buf, 3, "login_id" ,"text", "timestamp");
    if (msg != NULL) {
        goto cleanup_and_respond;
    }
    printf("chat_log : %s",cJSON_Print(chat_log));
    printf("\n");
    type = 14;

cleanup_and_respond:
    cJSON_AddNumberToObject(result_json, "type", type);
    cJSON_AddStringToObject(result_json, "groupname", cJSON_GetStringValue(groupname_ptr));
    if (msg != NULL) {
        cJSON_AddStringToObject(result_json, "msg", msg);
    }
    else {
        cJSON_AddItemToObject(result_json, "chatlog", chat_log);
    }

    response_str = cJSON_Print(result_json);
    reserve_epoll_send(server_ptr->epoll_fd, now_session, response_str, strlen(response_str));
    release_conns(&server_ptr->db, 2, log_conn, chat_group_conn);
    cJSON_del(2, result_json, json_ptr);
    free_all(1, response_str);
    return ;
}

void group_delete_service(epoll_net_core* server_ptr, task_t* task) {
    //printf("group_delete_service\n");
    int type = 100;
    char* msg = NULL;
    cJSON* json_ptr = NULL;
    cJSON* result_json = cJSON_CreateObject();
    client_session_t* now_session = NULL;
    conn_t* chat_group_conn = NULL;
    char* response_str = NULL;
    char SQL_buf[1024];

    now_session = find_session_by_fd(&server_ptr->session_pool, task->req_client_fd);
    if (now_session == NULL) {
        msg = "Session Error";
        goto cleanup_and_respond;
    }

    chat_group_conn = get_conn(&server_ptr->db.pools[CHAT_GROUP_DB_IDX]); 
    json_ptr = get_parsed_json(task->buf);
    if (json_ptr == NULL) {
        msg = "user send invalid json";
        goto cleanup_and_respond;
    }
    /*
    {
        "type": 15,
        "groupname": "그룹이름" 
    }
    */
    cJSON* groupname_ptr = cJSON_GetObjectItem(json_ptr, "groupname");
    if (groupname_ptr == NULL || cJSON_GetStringValue(groupname_ptr)[0] == '\0') {
        msg = "user send invalid json. Miss group_name";
        goto cleanup_and_respond;
    }

    // 채팅 그룹 마스터 권한 확인
    int uid = find(&server_ptr->fd_to_uid_hash, task->req_client_fd);
    if (uid < 0) {
        msg = "Invalid user";
        goto cleanup_and_respond;
    }

    snprintf(SQL_buf, sizeof(SQL_buf), 
        "SELECT gm.gid FROM group_member AS gm LEFT JOIN chat_group AS cg ON cg.gid = gm.gid \
        WHERE cg.groupname = '%s' AND gm.uid = %d AND gm.is_host = 1", cJSON_GetStringValue(groupname_ptr), uid);
    int gid = query_result_to_int(chat_group_conn, &msg, SQL_buf);
    if (gid < 0 || msg != NULL) {
        if (strcmp(msg, "No result") == 0) {
            msg = "Not permitted User";
        }
        goto cleanup_and_respond;
    }

    // 트랜젝션 커밋
    // mysql_rollback(chat_group_conn->conn);
    if (mysql_autocommit(chat_group_conn->conn, 0)) {
        msg = "transaction start fail";
        goto cleanup_and_respond;
    }
    // 그룹 맴버 삭제
    snprintf(SQL_buf, sizeof(SQL_buf), "DELETE FROM group_member WHERE gid = %d", gid);
    query_result_to_execuete(chat_group_conn, &msg, SQL_buf);
    if (msg != NULL) {
        mysql_rollback(chat_group_conn->conn);
        goto cleanup_and_respond;
    }
    // 그룹 삭제
    snprintf(SQL_buf, sizeof(SQL_buf), "DELETE FROM chat_group WHERE gid = %d", gid);
    query_result_to_execuete(chat_group_conn, &msg, SQL_buf);
    if (msg != NULL) {
        mysql_rollback(chat_group_conn->conn);
        goto cleanup_and_respond;
    }
    
    type = 15;

cleanup_and_respond:
    mysql_commit(chat_group_conn->conn);
    cJSON_AddNumberToObject(result_json, "type", type);
    if (msg != NULL) {
        cJSON_AddStringToObject(result_json, "msg", msg);
    }
    response_str = cJSON_Print(result_json);
    reserve_epoll_send(server_ptr->epoll_fd, now_session, response_str, strlen(response_str));
    release_conns(&server_ptr->db, 1, chat_group_conn);
    cJSON_del(2, json_ptr, result_json);
    free_all(1, response_str);
    return ;
}

void mng_server_log_service(epoll_net_core* server_ptr, task_t* task) {
    //printf("mng_server_log_service\n");
    int type = 100;
    char* msg = NULL;
    cJSON* json_ptr = NULL;
    cJSON* result_json = cJSON_CreateObject();
    cJSON* server_log_list = NULL;
    client_session_t* now_session = NULL;
    conn_t* user_setting_conn = NULL;
    conn_t* log_conn = NULL;
    char* response_str = NULL;
    char SQL_buf[1024];

    log_conn = get_conn(&server_ptr->db.pools[LOG_DB_IDX]);
    user_setting_conn = get_conn(&server_ptr->db.pools[USER_SETTING_DB_IDX]);

    now_session = find_session_by_fd(&server_ptr->session_pool, task->req_client_fd);
    if (now_session == NULL) {
        msg = "Session Error";
        goto cleanup_and_respond;
    }

    json_ptr = get_parsed_json(task->buf);
    if (json_ptr == NULL) {
        msg = "user send invalid json";
        goto cleanup_and_respond;
    }

    int uid = find(&server_ptr->fd_to_uid_hash, task->req_client_fd);
    if (uid < 0) {
        msg = "Invalid user";
        goto cleanup_and_respond;
    }

    cJSON* start_time_ptr = cJSON_GetObjectItem(json_ptr, "start_time");
    if (start_time_ptr == NULL || cJSON_GetStringValue(start_time_ptr)[0] == '\0') {
        msg = "user send invalid json. Miss start_time";
        goto cleanup_and_respond;
    }

    cJSON* end_time_ptr = cJSON_GetObjectItem(json_ptr, "end_time");
    if (end_time_ptr == NULL || cJSON_GetStringValue(end_time_ptr)[0] == '\0') {
        msg = "user send invalid json. Miss end_time";
        goto cleanup_and_respond;
    }

    // 관리자 권한 확인
    snprintf(SQL_buf, sizeof(SQL_buf), "SELECT uid FROM user WHERE uid = %d AND role = 1", uid);
    int rt_uid = query_result_to_int(user_setting_conn, &msg, SQL_buf);
    if (uid < 0 || msg != NULL) {
        if (strcmp(msg, "No result") == 0) {
            msg = "Not permitted User";
        }
        goto cleanup_and_respond;
    }

    // 채팅 로그 가져오기
    snprintf(SQL_buf, sizeof(SQL_buf), "SELECT uptime, downtime FROM server_log WHERE uptime BETWEEN '%s' AND '%s'", 
        cJSON_GetStringValue(start_time_ptr), cJSON_GetStringValue(end_time_ptr));
    server_log_list = query_result_to_json(log_conn, &msg, SQL_buf, 2, "uptime", "downtime");
    if (msg != NULL) {
        goto cleanup_and_respond;
    }

    type = 16;

cleanup_and_respond:
    cJSON_AddNumberToObject(result_json, "type", type);
    if (msg != NULL) {
        cJSON_AddStringToObject(result_json, "msg", msg);
    }
    else {
        cJSON_AddItemToObject(result_json, "server_log_list", server_log_list);
    }
    response_str = cJSON_Print(result_json);
    reserve_epoll_send(server_ptr->epoll_fd, now_session, response_str, strlen(response_str));
    release_conns(&server_ptr->db, 2, user_setting_conn, log_conn);
    cJSON_del(2, json_ptr, result_json);
    free_all(1, response_str);
    return ;
}

void mng_server_status_service(epoll_net_core* server_ptr, task_t* task) {
    //printf("mng_server_status_service\n");
    int type = 100;
    FILE* log_file = NULL;
    int log_file_fd = -1;
    char* msg = NULL;
    char *response_str = NULL;
    cJSON* result_json = cJSON_CreateObject();
    client_session_t* now_session = NULL;

    int login_user_cnt;
    int tps;
    double mem_usage;
    char line[40] = {0};
    char last_line[40] = {0};

    now_session = find_session_by_fd(&server_ptr->session_pool, task->req_client_fd);
    if (now_session == NULL) {
        msg = "Session Error";
        goto cleanup_and_respond;
    }

    log_file = fopen(LOG_FILE, "r");
    if (!log_file) {
        perror("Failed to open log file");
        msg = "server log error1";
        goto cleanup_and_respond;
    }
    log_file_fd = fileno(log_file);
    if (flock(log_file_fd, LOCK_EX) == -1) {
        perror("Failed to lock file");
        msg = "server log error2";
        goto cleanup_and_respond;
    }

    while (fgets(line, sizeof(line), log_file)) {
        strncpy(last_line, line, sizeof(last_line) - 1);
        last_line[sizeof(last_line) - 1] = '\0'; // Ensure null-terminated string
    }

    type = 17;

cleanup_and_respond:
    if (log_file_fd >= 0) {
        flock(log_file_fd, LOCK_UN);
    }
    if (log_file != NULL) {
        fclose(log_file);
    }

    cJSON_AddNumberToObject(result_json, "type", type);
    if (msg != NULL) {
        cJSON_AddStringToObject(result_json, "msg", msg);
    }
    else {
        if (last_line[0] != '\0') {
            struct tm log_time;
            sscanf(last_line, "[%d-%d-%d %d:%d:%d] %d %d %lf%%", 
                &log_time.tm_year, &log_time.tm_mon, &log_time.tm_mday,
                &log_time.tm_hour, &log_time.tm_min, &log_time.tm_sec,
                &login_user_cnt, &tps, &mem_usage);
            cJSON_AddNumberToObject(result_json, "mem", mem_usage);
            cJSON_AddNumberToObject(result_json, "login_user_cnt", login_user_cnt);
            cJSON_AddNumberToObject(result_json, "tps", tps);
        }
    }

    response_str = cJSON_Print(result_json);
    reserve_epoll_send(server_ptr->epoll_fd, now_session, response_str, strlen(response_str));
    cJSON_del(1, result_json);
    free_all(1, response_str);

    return ;
}

void chat_in_user_service(epoll_net_core* server_ptr, task_t* task) {
    //printf("chat_in_user_service\n");
    int type = 100;
    char* msg = NULL;
    cJSON* result_json = cJSON_CreateObject();
    client_session_t* now_session = NULL;
    client_session_t* recver_session = NULL;
    conn_t* user_setting_conn = NULL;
    conn_t* log_conn = NULL;
    char* response_str = NULL;
    int recieve_fd = -1;
    char* timestamp = NULL;
    char SQL_buf[1024];

    now_session = find_session_by_fd(&server_ptr->session_pool, task->req_client_fd);
    if (now_session == NULL) {
        msg = "Session Error";
        goto cleanup_and_respond;
    }

    cJSON* json_ptr = get_parsed_json(task->buf);
    if (json_ptr == NULL)
    {
        msg = "user send invalid json";
        goto cleanup_and_respond;
    }
    // 유저 로그인 확인
    int uid = find(&server_ptr->fd_to_uid_hash, task->req_client_fd);
    if (uid < 0) {
        msg = "Invalid user";
        goto cleanup_and_respond;
    }

    cJSON* sender_login_id_ptr = cJSON_GetObjectItem(json_ptr, "sender_login_id");
    if (sender_login_id_ptr == NULL || cJSON_GetStringValue(sender_login_id_ptr)[0] == '\0')
    {
        msg = "user send invalid json. Miss sender_login_id_ptr";
        goto cleanup_and_respond;
    }

    cJSON* recver_login_id_ptr = cJSON_GetObjectItem(json_ptr, "recver_login_id");
    if (recver_login_id_ptr == NULL || cJSON_GetStringValue(recver_login_id_ptr)[0] == '\0')
    {
        msg = "user send invalid json. Miss recver_login_id";
        goto cleanup_and_respond;
    }

    cJSON* text_ptr = cJSON_GetObjectItem(json_ptr, "text");
    if (text_ptr == NULL || cJSON_GetStringValue(text_ptr)[0] == '\0')
    {
        msg = "user send invalid json. Miss text";
        goto cleanup_and_respond;
    }

    user_setting_conn = get_conn(&server_ptr->db.pools[USER_SETTING_DB_IDX]);
    log_conn = get_conn(&server_ptr->db.pools[LOG_DB_IDX]);
    

    snprintf(SQL_buf, sizeof(SQL_buf), 
        "SELECT uid FROM user WHERE login_id = '%s'", cJSON_GetStringValue(recver_login_id_ptr));
    int recver_uid = query_result_to_int(user_setting_conn, &msg, SQL_buf);
    if (msg != NULL) {
        goto cleanup_and_respond;
    }
   
    snprintf(SQL_buf, sizeof(SQL_buf), 
        "SELECT max_tps FROM user WHERE uid = %d", uid);
    int max_tps = query_result_to_int(user_setting_conn, &msg, SQL_buf);
    if (msg != NULL) {
        goto cleanup_and_respond;
    }

    snprintf(SQL_buf, sizeof(SQL_buf), 
        "CALL dm_message(%d, '%s', %d, '%s', '%s', '%d', @result)", 
        uid, cJSON_GetStringValue(sender_login_id_ptr), recver_uid, cJSON_GetStringValue(recver_login_id_ptr), cJSON_GetStringValue(text_ptr), max_tps);
    query_result_to_execuete(log_conn, &msg, SQL_buf);
    if (msg != NULL) {
        goto cleanup_and_respond;
    }

    snprintf(SQL_buf, sizeof(SQL_buf), "SELECT @result");
    timestamp = query_result_to_str(log_conn, &msg, SQL_buf);
    if (msg != NULL) {
        goto cleanup_and_respond;
    }
    if (timestamp == NULL) {
        type = 101;
        msg = "Too Much Message in Minute";
        goto cleanup_and_respond;
    }

    type = DM_SERV_FUNC;

    recieve_fd = find(&server_ptr->uid_to_fd_hash, recver_uid);
    if (recieve_fd < 0) {
        // 상대 접속 안했으면 경고 안띄우게 하기 위해 주석.
        //msg = "user is not online";
        goto cleanup_and_respond;
    }
    recver_session = find_session_by_fd(&server_ptr->session_pool, recieve_fd);
    if (recver_session == NULL) {
        msg = "server session error";
        goto cleanup_and_respond;
    }
    // if (recver_session == now_session) {
    //     goto cleanup_and_respond;
    // }

cleanup_and_respond:
    cJSON_AddNumberToObject(result_json, "type", type);
    if (msg != NULL) {
        cJSON_AddStringToObject(result_json, "msg", msg);
    }
    else {
        cJSON_AddStringToObject(result_json, "sender_login_id", cJSON_GetStringValue(sender_login_id_ptr));
        cJSON_AddStringToObject(result_json, "recver_login_id", cJSON_GetStringValue(recver_login_id_ptr));
        cJSON_AddStringToObject(result_json, "text", cJSON_GetStringValue(text_ptr));
        cJSON_AddStringToObject(result_json, "timestamp", timestamp);
    }
    response_str = cJSON_Print(result_json);
    reserve_epoll_send(server_ptr->epoll_fd, now_session, response_str, strlen(response_str));
    if (recver_session != NULL && recver_session != now_session) {
        reserve_epoll_send(server_ptr->epoll_fd, recver_session, response_str, strlen(response_str));
    }
    release_conns(&server_ptr->db, 2, log_conn, user_setting_conn);
    cJSON_del(2, json_ptr, result_json);
    free_all(2, response_str, timestamp);
    return ;
}

void pre_dm_log_service(epoll_net_core* server_ptr, task_t* task) {
    //("pre_dm_log_service\n");
    int type = 100;
    char* msg = NULL;
    char *response_str = NULL;
    cJSON* result_json = cJSON_CreateObject();
    client_session_t* now_session = NULL;
    conn_t* log_conn = NULL;
    conn_t* user_setting_conn = NULL;
    char SQL_buf[1024];
    char uid_list_str[1024] = "";
    int recver_uid = 0;

    now_session = find_session_by_fd(&server_ptr->session_pool, task->req_client_fd);
    if (now_session == NULL) {
        msg = "Session Error";
        goto cleanup_and_respond;
    }

    log_conn = get_conn(&server_ptr->db.pools[LOG_DB_IDX]);
    user_setting_conn = get_conn(&server_ptr->db.pools[USER_SETTING_DB_IDX]);    

    int uid = find(&server_ptr->fd_to_uid_hash, task->req_client_fd);
    if (uid < 0) {
        msg = "Invalid user";
        goto cleanup_and_respond;
    }

    cJSON* json_ptr = get_parsed_json(task->buf);
    if (json_ptr == NULL) {
        msg = "user send invalid json";
        goto cleanup_and_respond;
    }

    cJSON* recver_login_id_ptr = cJSON_GetObjectItem(json_ptr, "recver_login_id");
    if (recver_login_id_ptr == NULL || cJSON_GetStringValue(recver_login_id_ptr)[0] == '\0') {
        msg = "user send invalid json. Miss groupname";
        goto cleanup_and_respond;
    }
    cJSON* start_time_ptr = cJSON_GetObjectItem(json_ptr, "start_time");
    if (start_time_ptr == NULL || cJSON_GetStringValue(start_time_ptr)[0] == '\0') {
        msg = "user send invalid json. Miss start_time";
        goto cleanup_and_respond;
    }
    cJSON* end_time_ptr = cJSON_GetObjectItem(json_ptr, "end_time");
    if (end_time_ptr == NULL || cJSON_GetStringValue(end_time_ptr)[0] == '\0') {
        msg = "user send invalid json. Miss end_time";
        goto cleanup_and_respond;
    }

    snprintf(SQL_buf, sizeof(SQL_buf), "SELECT uid FROM user WHERE login_id = '%s'",
    cJSON_GetStringValue(recver_login_id_ptr));
    recver_uid = query_result_to_int(user_setting_conn, &msg, SQL_buf);
    if (msg != NULL) {
        goto cleanup_and_respond;
    }

    snprintf(SQL_buf, sizeof(SQL_buf), "SELECT sender_login_id, text, timestamp FROM dm_log WHERE (recver_uid = %d AND sender_uid = %d) OR \
    (recver_uid = %d AND sender_uid = %d) AND timestamp BETWEEN '%s' AND '%s' ORDER BY timestamp ASC",
    recver_uid,uid,uid,recver_uid,cJSON_GetStringValue(start_time_ptr),cJSON_GetStringValue(end_time_ptr));
    cJSON* chat_log = query_result_to_json(log_conn, &msg, SQL_buf, 3, "sender_login_id" ,"text", "timestamp");
    if (msg != NULL) {
        goto cleanup_and_respond;
    }
    type = 19;

cleanup_and_respond:
    cJSON_AddNumberToObject(result_json, "type", type);
    if (msg != NULL) {
        cJSON_AddStringToObject(result_json, "msg", msg);
    }
    else {
        cJSON_AddItemToObject(result_json, "dmlog", chat_log);
    }

    response_str = cJSON_Print(result_json);
    reserve_epoll_send(server_ptr->epoll_fd, now_session, response_str, strlen(response_str));
    release_conns(&server_ptr->db, 2, log_conn, user_setting_conn);
    cJSON_del(2, result_json, json_ptr);
    free_all(1, response_str);
    return ;
}

void out_chat_group_service(epoll_net_core* server_ptr, task_t* task) {
    //printf("out_chat_group_service\n");
    int type = 100;
    char* msg = NULL;
    char *response_str = NULL;
    cJSON* result_json = cJSON_CreateObject();
    client_session_t* now_session = NULL;
    conn_t* chat_group_conn = NULL;
    char SQL_buf[512];
    int gid = -1;

    chat_group_conn = get_conn(&server_ptr->db.pools[CHAT_GROUP_DB_IDX]);

    now_session = find_session_by_fd(&server_ptr->session_pool, task->req_client_fd);
    if (now_session == NULL)
    {
        msg = "session error";
        goto cleanup_and_respond;
    }

    int uid = find(&server_ptr->fd_to_uid_hash, task->req_client_fd);
    if (uid < 0) {
        msg = "Invalid user";
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
        msg = "user send invalid json. Miss page";
        goto cleanup_and_respond;
    }

    snprintf(SQL_buf, sizeof(SQL_buf), "SELECT gid FROM chat_group WHERE groupname = '%s'",cJSON_GetStringValue(groupname_ptr));
    gid = query_result_to_int(chat_group_conn, &msg, SQL_buf);
    if ((gid == -1) || (msg != NULL)) {
        goto cleanup_and_respond;
    }

    snprintf(SQL_buf, sizeof(SQL_buf), "DELETE FROM group_member WHERE gid = %d AND uid = %d",gid,uid);
    query_result_to_execuete(chat_group_conn, &msg, SQL_buf);
    if ((msg != NULL)) {
        goto cleanup_and_respond;
    }

    type = 20;

cleanup_and_respond:
    cJSON_AddNumberToObject(result_json, "type", type);
    if (msg != NULL) {
        cJSON_AddStringToObject(result_json, "msg", msg);
    }
    response_str = cJSON_Print(result_json);
    reserve_epoll_send(server_ptr->epoll_fd, now_session, response_str, strlen(response_str));
    release_conns(&server_ptr->db, 1, chat_group_conn);
    cJSON_del(2, result_json, json_ptr);
    free_all(1, response_str);
    return ;
}

// Notice의 경우, conn을 외부에서 받아서 쓸 것! (conn 또 할당받으면 재귀적 자원 할당으로 데드락 가능성 높음)
void user_status_change_notice(epoll_net_core* server_ptr, conn_t* user_setting_conn) {
    char* error_msg = NULL;
    char* response_str = NULL;
    cJSON* result_json = cJSON_CreateObject();
    char notice_send_buf[50];
    int* keys;
    size_t num_keys = get_all_keys(&server_ptr->fd_to_uid_hash, &keys);

    // printf("Number of keys: %zu\n", num_keys);
    // for (size_t i = 0; i < num_keys; ++i) {
    //     printf("Key %zu: %d\n", i, keys[i]);
    // }

    cJSON_AddNumberToObject(result_json, "type", USER_STATUS_CHANGE_NOTICE);
    response_str = cJSON_Print(result_json);
    for (size_t i = 0; i < num_keys; i++) {
        int client_fd = keys[i];
        if (client_fd < 0) {
            continue ;
        }
        reserve_epoll_send(server_ptr->epoll_fd, find_session_by_fd(&server_ptr->session_pool,client_fd), response_str, strlen(response_str));
    }

cleanup_and_respond:
    if (error_msg != NULL) {
        fprintf(stderr, "%s", error_msg);
    }
    cJSON_del(1, result_json);
    free_all(2, response_str,keys);
    return ;
}

int set_send_timeout(int sockfd, int seconds) {
    struct timeval timeout;
    timeout.tv_sec = seconds;
    timeout.tv_usec = 0;

    if (setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
        perror("setsockopt");
        return -1;
    }
    return 0;
}

void server_down_notice_to_all(epoll_net_core* server_ptr) {
    char* error_msg = NULL;
    char *response_str = NULL;
    cJSON* result_json = cJSON_CreateObject();
    char notice_send_buf[50];
    int notice_total_size;

    cJSON_AddNumberToObject(result_json, "type", SERVER_DOWN_NOTICE);
    response_str = cJSON_Print(result_json);
    notice_total_size = HEADER_SIZE + strlen(response_str);
    memcpy(notice_send_buf, (char*)(&notice_total_size), HEADER_SIZE);
    memcpy(notice_send_buf + HEADER_SIZE, response_str, notice_total_size - HEADER_SIZE);
    for (int i = 0; i < MAX_CLIENT_NUM; i++) {
        if (server_ptr->session_pool.session_pool[i].fd < 0) {
            continue ;
        }
        int client_fd = server_ptr->session_pool.session_pool[i].fd;
        if (set_send_timeout(client_fd, 1) < 0) {
            close(client_fd);
        }
        send(client_fd, notice_send_buf, notice_total_size, 0);
    }

cleanup_and_respond:
    if (error_msg != NULL) {
        fprintf(stderr, "%s", error_msg);
    }
    cJSON_del(1, result_json);
    free_all(1, response_str);
    return ;
}

void user_log_service(epoll_net_core* server_ptr, task_t* task) {
    //printf("user_log_service\n");
    int type = 100;
    char* msg = NULL;
    cJSON* json_ptr = NULL;
    cJSON* result_json = cJSON_CreateObject();
    cJSON* user_log_list = NULL;
    client_session_t* now_session = NULL;
    conn_t* user_setting_conn = NULL;
    conn_t* log_conn = NULL;
    char* response_str = NULL;
    char SQL_buf[1024];

    now_session = find_session_by_fd(&server_ptr->session_pool, task->req_client_fd);
    if (now_session == NULL) {
        msg = "Session Error";
        goto cleanup_and_respond;
    }

    log_conn = get_conn(&server_ptr->db.pools[LOG_DB_IDX]);
    user_setting_conn = get_conn(&server_ptr->db.pools[USER_SETTING_DB_IDX]);
    json_ptr = get_parsed_json(task->buf);
    if (json_ptr == NULL) {
        msg = "user send invalid json";
        goto cleanup_and_respond;
    }

    int uid = find(&server_ptr->fd_to_uid_hash, task->req_client_fd);
    if (uid < 0) {
        msg = "Invalid user";
        goto cleanup_and_respond;
    }

    cJSON* start_time_ptr = cJSON_GetObjectItem(json_ptr, "start_time");
    if (start_time_ptr == NULL || cJSON_GetStringValue(start_time_ptr)[0] == '\0') {
        msg = "user send invalid json. Miss start_time";
        goto cleanup_and_respond;
    }

    cJSON* end_time_ptr = cJSON_GetObjectItem(json_ptr, "end_time");
    if (end_time_ptr == NULL || cJSON_GetStringValue(end_time_ptr)[0] == '\0') {
        msg = "user send invalid json. Miss end_time";
        goto cleanup_and_respond;
    }

    // 관리자 권한 확인
    snprintf(SQL_buf, sizeof(SQL_buf), "SELECT uid FROM user WHERE uid = %d AND role = 1", uid);
    int rt_uid = query_result_to_int(user_setting_conn, &msg, SQL_buf);
    if (uid < 0 || msg != NULL) {
        if (strcmp(msg, "No result") == 0) {
            msg = "Not permitted User";
        }
        goto cleanup_and_respond;
    }

    snprintf(SQL_buf, sizeof(SQL_buf), "SELECT login_time, logout_time, login_id FROM client_log WHERE login_time BETWEEN '%s' AND '%s'", 
        cJSON_GetStringValue(start_time_ptr), cJSON_GetStringValue(end_time_ptr));
    user_log_list = query_result_to_json(log_conn, &msg, SQL_buf, 3, "login_time", "logout_time", "login_id");
    if (msg != NULL) {
        goto cleanup_and_respond;
    }

    type = 21;

cleanup_and_respond:
    cJSON_AddNumberToObject(result_json, "type", type);
    if (msg != NULL) {
        cJSON_AddStringToObject(result_json, "msg", msg);
    }
    else {
        cJSON_AddItemToObject(result_json, "user_log_list", user_log_list);
    }
    response_str = cJSON_Print(result_json);
    reserve_epoll_send(server_ptr->epoll_fd, now_session, response_str, strlen(response_str));
    release_conns(&server_ptr->db, 2, user_setting_conn, log_conn);
    cJSON_del(2, json_ptr, result_json);
    free_all(1, response_str);
    return ;
}

void current_user_list_service(epoll_net_core* server_ptr, task_t* task) {
    //printf("current_user_list_service\n");
    int type = 100;
    char* msg = NULL;
    cJSON* json_ptr = NULL;
    cJSON* result_json = cJSON_CreateObject();
    cJSON* user_log_list = NULL;
    client_session_t* now_session = NULL;
    conn_t* log_conn = NULL;
    char* response_str = NULL;
    char SQL_buf[1024];

    now_session = find_session_by_fd(&server_ptr->session_pool, task->req_client_fd);
    if (now_session == NULL) {
        msg = "Session Error";
        goto cleanup_and_respond;
    }

    log_conn = get_conn(&server_ptr->db.pools[LOG_DB_IDX]);

    snprintf(SQL_buf, sizeof(SQL_buf), "SELECT login_id FROM client_log WHERE logout_time IS NULL");
    user_log_list = query_result_to_json(log_conn, &msg, SQL_buf, 1, "login_id");
    if (msg != NULL) {
        goto cleanup_and_respond;
    }

    type = 22;

cleanup_and_respond:
    cJSON_AddNumberToObject(result_json, "type", type);
    if (msg != NULL) {
        cJSON_AddStringToObject(result_json, "msg", msg);
    }
    else {
        cJSON_AddItemToObject(result_json, "current_user_list", user_log_list);
    }
    response_str = cJSON_Print(result_json);
    reserve_epoll_send(server_ptr->epoll_fd, now_session, response_str, strlen(response_str));
    release_conns(&server_ptr->db, 1 , log_conn);
    cJSON_del(2, json_ptr, result_json);
    free_all(1, response_str);
    return ;
}