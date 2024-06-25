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
    cJSON_AddStringToObject(result_json, "login_id", cJSON_GetStringValue(id_ptr));
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
    char* msg = NULL;
    cJSON* result_json = cJSON_CreateObject();
    client_session_t* now_session = NULL;
    conn_t* chat_group_conn = NULL;
    char SQL_buf[512];

    chat_group_conn = get_conn(&server_ptr->db.pools[CHAT_GROUP_DB_IDX]);

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
    char *response_str = cJSON_Print(result_json);
    reserve_epoll_send(server_ptr->epoll_fd, now_session, response_str, strlen(response_str));
    release_conns(&server_ptr->db, 1, chat_group_conn);
    cJSON_Delete(json_ptr);
    cJSON_Delete(result_json);
    return ;
}

void edit_member_service(epoll_net_core* server_ptr, task_t* task) {
    printf("add_member_service\n");
    int type = 100;
    char* msg = NULL;
    cJSON* result_json = cJSON_CreateObject();
    client_session_t* now_session = NULL;
    conn_t* chat_group_conn = NULL;
    conn_t* user_setting_conn = NULL;
    char SQL_buf[512];
    int array_size = 0;

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
        if (user_item == NULL) {
            msg = "Invalid JSON: Empty username in users list";
            goto cleanup_and_respond;
        }
        else if (cJSON_GetStringValue(user_item)[0] != '\0') {
            snprintf(SQL_buf, sizeof(SQL_buf), "SELECT uid FROM user WHERE login_id = '%s'", cJSON_GetStringValue(user_item));
            int uid = query_result_to_int(user_setting_conn,&msg,SQL_buf);

            snprintf(SQL_buf, sizeof(SQL_buf), "INSERT INTO group_member (uid, gid,is_host) VALUES ('%d', '%d',0)", uid, gid);
            query_result_to_execuete(chat_group_conn,&msg,SQL_buf);
            if (msg != NULL) {
                msg = "rollback";
                mysql_rollback(user_setting_conn->conn);
                goto cleanup_and_respond;
            }
        }
    }

    array_size = cJSON_GetArraySize(outmem_ptr);
    for (int i = 0; i < array_size; i++) {
        cJSON* user_item = cJSON_GetArrayItem(outmem_ptr, i);
        if (user_item == NULL) {
            msg = "Invalid JSON: Empty username in users list";
            goto cleanup_and_respond;
        }
        else if (cJSON_GetStringValue(user_item)[0] != '\0') {
            snprintf(SQL_buf, sizeof(SQL_buf), "SELECT uid FROM user WHERE login_id = '%s'", cJSON_GetStringValue(user_item));
            int uid = query_result_to_int(user_setting_conn,&msg,SQL_buf);

            snprintf(SQL_buf, sizeof(SQL_buf), "DELETE FROM group_member WHERE uid = %d)", uid);
            query_result_to_execuete(chat_group_conn,&msg,SQL_buf);
            if (msg != NULL) {
                msg = "rollback";
                mysql_rollback(user_setting_conn->conn);
                goto cleanup_and_respond;
            }
        }
    }

    mysql_commit(user_setting_conn->conn);
    type = 7;

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

void Mng_req_list_service(epoll_net_core* server_ptr, task_t* task) {
    printf("Mng_req_list_servce\n");
    int type = 100;
    char* msg = NULL;
    cJSON* result_json = cJSON_CreateObject();
    client_session_t* now_session = NULL;
    conn_t* user_setting_conn = NULL;
    conn_t* chat_group_conn = NULL;
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

void Mng_signup_approve_service(epoll_net_core* server_ptr, task_t* task) {
    printf("Mng_signup_approve_service\n");
    int type = 100;
    char* msg = NULL;
    cJSON* result_json = cJSON_CreateObject();
    client_session_t* now_session = NULL;
    conn_t* user_setting_conn = NULL;
    char SQL_buf[512];

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

    if (mysql_autocommit(user_setting_conn->conn, 0)) {
        msg = "transaction fail";
        goto cleanup_and_respond;
    }

    if (cJSON_GetNumberValue(approve_ptr) == 0) {
        snprintf(SQL_buf, sizeof(SQL_buf),"DELETE FROM signup_req WHERE login_id = '%s'",cJSON_GetStringValue(id_ptr));
        query_result_to_execuete(user_setting_conn, &msg, SQL_buf);
        if (msg != NULL) {
            msg = "rollback";
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
    snprintf(SQL_buf, sizeof(SQL_buf), "SELECT login_id, password, name, phone, email FROM signup_req WHERE login_id = '%s'",cJSON_GetStringValue(id_ptr));

    cJSON* user_data_array = query_result_to_json(user_setting_conn, &msg, SQL_buf, 5, "login_id", "password", "name", "phone", "email");
    if (msg != NULL) {
        goto cleanup_and_respond;
    }
    cJSON* user_data_json = cJSON_GetArrayItem(user_data_array,0);

    snprintf(SQL_buf, sizeof(SQL_buf), 
             "INSERT INTO user (login_id, password, name, phone, email, did, position, role, create_date, max_tps) VALUES ('%s', '%s', '%s', '%s', '%s', %d, %d, %d, NOW(), %d)",
             cJSON_GetStringValue(cJSON_GetObjectItem(user_data_json, "login_id")), 
             cJSON_GetStringValue(cJSON_GetObjectItem(user_data_json, "password")), 
             cJSON_GetStringValue(cJSON_GetObjectItem(user_data_json, "name")), 
             cJSON_GetStringValue(cJSON_GetObjectItem(user_data_json, "phone")), 
             cJSON_GetStringValue(cJSON_GetObjectItem(user_data_json, "email")), 
             dept, 
             pos, 
             role, 
             max_tps);
    
    query_result_to_execuete(user_setting_conn, &msg, SQL_buf);
    if (msg != NULL) {
        msg = "rollback";
        mysql_rollback(user_setting_conn->conn);
        goto cleanup_and_respond;
    }

    snprintf(SQL_buf, sizeof(SQL_buf),"DELETE FROM signup_req WHERE login_id = '%s'",cJSON_GetStringValue(id_ptr));
    query_result_to_execuete(user_setting_conn, &msg, SQL_buf);
    if (msg != NULL) {
        msg = "rollback";
        mysql_rollback(user_setting_conn->conn);
        goto cleanup_and_respond;
    }
    mysql_commit(user_setting_conn->conn);
    type = 9;


cleanup_and_respond:
    cJSON_AddNumberToObject(result_json, "type", type);
    if (msg != NULL)
    {
        cJSON_AddStringToObject(result_json, "msg", msg);
    }

    char *response_str = cJSON_Print(result_json);
    reserve_epoll_send(server_ptr->epoll_fd, now_session, response_str, strlen(response_str));
    release_conns(&server_ptr->db, 1, user_setting_conn);
    cJSON_Delete(json_ptr);
    cJSON_Delete(result_json);
    return ;
}

void Mng_group_approve_service(epoll_net_core* server_ptr, task_t* task) {
    printf("Mng_group_approve_service\n");
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
    snprintf(SQL_buf,sizeof(SQL_buf),"SELECT uid FROM group_req WHERE groupname = '%s'",cJSON_GetStringValue(groupname_ptr));
    int uid_value = query_result_to_int(chat_group_conn, &msg, SQL_buf);
    
    if (mysql_autocommit(chat_group_conn->conn, 0)) {
        msg = "transaction fail";
        goto cleanup_and_respond;
    }
    if (cJSON_GetNumberValue(approve_ptr) == 0) {
        snprintf(SQL_buf, sizeof(SQL_buf),"DELETE FROM group_req WHERE uid = %d",uid_value);
        query_result_to_execuete(chat_group_conn, &msg, SQL_buf);
        if (msg != NULL) {
            mysql_rollback(chat_group_conn->conn);
            goto cleanup_and_respond;
        }
        type = 10;
        goto cleanup_and_respond;
    }

    snprintf(SQL_buf, sizeof(SQL_buf),"INSERT INTO chat_group (groupname) VALUES ('%s')",cJSON_GetStringValue(groupname_ptr));
    query_result_to_execuete(chat_group_conn, &msg, SQL_buf);
    if (msg != NULL) {
        mysql_rollback(chat_group_conn->conn);
        goto cleanup_and_respond;
    }

    snprintf(SQL_buf, sizeof(SQL_buf),"SELECT gid FROM chat_group WHERE groupname = '%s'",cJSON_GetStringValue(groupname_ptr));
    int gid_value = query_result_to_int(chat_group_conn, &msg, SQL_buf);
    if (msg != NULL) {
        mysql_rollback(chat_group_conn->conn);
        goto cleanup_and_respond;
    }

    snprintf(SQL_buf, sizeof(SQL_buf),"INSERT INTO group_member (uid, gid,is_host) VALUES ('%d','%d',1)",uid_value,gid_value);
    query_result_to_execuete(chat_group_conn, &msg, SQL_buf);
    if (msg != NULL) {
        mysql_rollback(chat_group_conn->conn);
        goto cleanup_and_respond;
    }

    snprintf(SQL_buf, sizeof(SQL_buf),"DELETE FROM group_req WHERE groupname = '%s'",cJSON_GetStringValue(groupname_ptr));
    query_result_to_execuete(chat_group_conn, &msg, SQL_buf);
    if (msg != NULL) {
        mysql_rollback(chat_group_conn->conn);
        goto cleanup_and_respond;
    }

    mysql_commit(chat_group_conn->conn);
    type = 10;


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

void group_member_service(epoll_net_core* server_ptr, task_t* task) {
    printf("group_member_service\n");
    int type = 100;
    char* msg = NULL;
    cJSON* result_json = cJSON_CreateObject();
    client_session_t* now_session = NULL;
    conn_t* user_setting_conn = NULL;
    conn_t* chat_group_conn = NULL;
    char SQL_buf[512];
    char uid_list_str[1024] = "";

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
    char *response_str = cJSON_Print(result_json);
    reserve_epoll_send(server_ptr->epoll_fd, now_session, response_str, strlen(response_str));
    release_conns(&server_ptr->db, 2, user_setting_conn, chat_group_conn);
    cJSON_Delete(json_ptr);
    cJSON_Delete(result_json);
    return ;
}

void chat_in_group_service(epoll_net_core* server_ptr, task_t* task) {
    printf("chat_in_group_service\n");
    int type = 100;
    char* msg = NULL;
    cJSON* result_json = cJSON_CreateObject();
    client_session_t* now_session = NULL;
    conn_t* user_setting_conn = NULL;
    conn_t* chat_group_conn = NULL;
    conn_t* log_conn = NULL;
    int* recieve_fd_array = NULL;
    char SQL_buf[1024];

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

    cJSON* test_ptr = cJSON_GetObjectItem(json_ptr, "text");
    if (test_ptr == NULL || cJSON_GetStringValue(test_ptr)[0] == '\0')
    {
        msg = "user send invalid json. Miss text";
        goto cleanup_and_respond;
    }

    now_session = find_session_by_fd(&server_ptr->session_pool, task->req_client_fd);
    if (now_session == NULL) {
        msg = "Session Error";
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
        "CALL insert_message(%d, %d, '%s', %d, @result)", 
        uid, gid, cJSON_GetStringValue(test_ptr), max_tps);
    query_result_to_execuete(log_conn, &msg, SQL_buf);
    if (msg != NULL) {
        goto cleanup_and_respond;
    }

    snprintf(SQL_buf, sizeof(SQL_buf), "SELECT @result");
    int tps_query_result = query_result_to_int(log_conn, &msg, SQL_buf);
    if (msg != NULL) {
        goto cleanup_and_respond;
    }
    if (tps_query_result == 0) {
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

    for (int i = 0; i < uid_count; i++) {
        printf("%d \n", recieve_fd_array[i]); write(STDOUT_FILENO, task->buf, task->task_data_len); write(STDOUT_FILENO, "\n", 1);
        if (recieve_fd_array[i] < 0) {
            continue;
        }
        client_session_t* session = find_session_by_fd(&server_ptr->session_pool, recieve_fd_array[i]);
        if (session == NULL) {
            printf("%d fd Not have session!!\n", recieve_fd_array[i]);
            continue;
        }
        // 그대로 echo때려버리면 될듯.
        reserve_epoll_send(server_ptr->epoll_fd, session, task->buf, task->task_data_len);
    }


cleanup_and_respond:
    if (msg != NULL) {
        cJSON_AddNumberToObject(result_json, "type", type);
        cJSON_AddStringToObject(result_json, "msg", msg);
        char *response_str = cJSON_Print(result_json);
        reserve_epoll_send(server_ptr->epoll_fd, now_session, response_str, strlen(response_str));
    }
    release_conns(&server_ptr->db, 3, log_conn, chat_group_conn, user_setting_conn);
    cJSON_Delete(json_ptr);
    cJSON_Delete(result_json);
    free(recieve_fd_array);
    return ;
}

void edit_user_info_service(epoll_net_core* server_ptr, task_t* task) {
    int type = 100;
    char* msg = NULL;
    cJSON* result_json = cJSON_CreateObject();
    client_session_t* now_session = NULL;
    conn_t* user_setting_conn = NULL;
    char SQL_buf[1024];

    if (mysql_autocommit(user_setting_conn->conn, 0)) {
        msg = "transaction fail";
        goto cleanup_and_respond;
    }

    cJSON* json_ptr = get_parsed_json(task->buf);
    if (json_ptr == NULL)
    {
        msg = "user send invalid json";
        goto cleanup_and_respond;
    }
    cJSON* login_id_ptr = cJSON_GetObjectItem(json_ptr, "login_id");
    if (login_id_ptr == NULL || cJSON_GetStringValue(login_id_ptr)[0] == '\0')
    {
        msg = "user send invalid json. Miss login_id";
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
            msg = "rollback";
            mysql_rollback(user_setting_conn->conn);
            goto cleanup_and_respond;
        }
    }
    cJSON* test_ptr = cJSON_GetObjectItem(json_ptr, "phone");
    if (name_ptr == NULL) {
        msg = "user send invalid json. Miss name";
        goto cleanup_and_respond;
    }
    else if (cJSON_GetStringValue(name_ptr)[0] != '\0') {
        snprintf(SQL_buf, sizeof(SQL_buf), "UPDATE user SET name = '%s' WHERE login_id = '%s'",cJSON_GetStringValue(name_ptr),cJSON_GetStringValue(login_id_ptr));
        query_result_to_execuete(user_setting_conn, &msg, SQL_buf);
        if (msg != NULL) {
            msg = "rollback";
            mysql_rollback(user_setting_conn->conn);
            goto cleanup_and_respond;
        }
    }
    cJSON* email_ptr = cJSON_GetObjectItem(json_ptr, "email");
    if (email_ptr == NULL) {
        msg = "user send invalid json. Miss email";
        goto cleanup_and_respond;
    }
    else if (cJSON_GetStringValue(name_ptr)[0] != '\0') {
        snprintf(SQL_buf, sizeof(SQL_buf), "UPDATE user SET email = '%s' WHERE login_id = '%s'",cJSON_GetStringValue(email_ptr),cJSON_GetStringValue(login_id_ptr));
        query_result_to_execuete(user_setting_conn, &msg, SQL_buf);
        if (msg != NULL) {
            msg = "rollback";
            mysql_rollback(user_setting_conn->conn);
            goto cleanup_and_respond;
        }
    }
    cJSON* dept_ptr = cJSON_GetObjectItem(json_ptr, "dept");
    if (dept_ptr == NULL) {
        msg = "user send invalid json. Miss dept";
        goto cleanup_and_respond;
    }
    else if (cJSON_GetStringValue(dept_ptr)[0] != '\0') {
        snprintf(SQL_buf, sizeof(SQL_buf), "UPDATE user SET dept = '%s' WHERE login_id = '%s'",cJSON_GetNumberValue(dept_ptr),cJSON_GetStringValue(login_id_ptr));
        query_result_to_execuete(user_setting_conn, &msg, SQL_buf);
        if (msg != NULL) {
            msg = "rollback";
            mysql_rollback(user_setting_conn->conn);
            goto cleanup_and_respond;
        }
    }
    cJSON* pos_ptr = cJSON_GetObjectItem(json_ptr, "pos");
    if (pos_ptr == NULL) {
        msg = "user send invalid json. Miss pos";
        goto cleanup_and_respond;
    }
    else if (cJSON_GetStringValue(pos_ptr)[0] != '\0') {
        snprintf(SQL_buf, sizeof(SQL_buf), "UPDATE user SET pos = '%s' WHERE login_id = '%s'",cJSON_GetNumberValue(pos_ptr),cJSON_GetStringValue(login_id_ptr));
        query_result_to_execuete(user_setting_conn, &msg, SQL_buf);
        if (msg != NULL) {
            msg = "rollback";
            mysql_rollback(user_setting_conn->conn);
            goto cleanup_and_respond;
        }
    }
    cJSON* role_ptr = cJSON_GetObjectItem(json_ptr, "role");
    if (role_ptr == NULL) {
        msg = "user send invalid json. Miss role";
        goto cleanup_and_respond;
    }
    else if (cJSON_GetStringValue(dept_ptr)[0] != '\0') {
        snprintf(SQL_buf, sizeof(SQL_buf), "UPDATE user SET role = '%s' WHERE login_id = '%s'",cJSON_GetNumberValue(role_ptr),cJSON_GetStringValue(login_id_ptr));
        query_result_to_execuete(user_setting_conn, &msg, SQL_buf);
        if (msg != NULL) {
            msg = "rollback";
            mysql_rollback(user_setting_conn->conn);
            goto cleanup_and_respond;
        }
    }
    cJSON* max_tps_ptr = cJSON_GetObjectItem(json_ptr, "max_tps");
    if (max_tps_ptr == NULL) {
        msg = "user send invalid json. Miss max_tps";
        goto cleanup_and_respond;
    }
    else if (cJSON_GetStringValue(max_tps_ptr)[0] != '\0') {
        snprintf(SQL_buf, sizeof(SQL_buf), "UPDATE user SET max_tps = '%s' WHERE login_id = '%s'",cJSON_GetNumberValue(max_tps_ptr),cJSON_GetStringValue(login_id_ptr));
        query_result_to_execuete(user_setting_conn, &msg, SQL_buf);
        if (msg != NULL) {
            msg = "rollback";
            mysql_rollback(user_setting_conn->conn);
            goto cleanup_and_respond;
        }
    }

    now_session = find_session_by_fd(&server_ptr->session_pool, task->req_client_fd);
    if (now_session == NULL) {
        msg = "Session Error";
        goto cleanup_and_respond;
    }

    mysql_commit(user_setting_conn->conn);
    type = 13;

cleanup_and_respond:
    if (msg != NULL) {
        cJSON_AddNumberToObject(result_json, "type", type);
        cJSON_AddStringToObject(result_json, "msg", msg);
        char *response_str = cJSON_Print(result_json);
        reserve_epoll_send(server_ptr->epoll_fd, now_session, response_str, strlen(response_str));
    }
    release_conns(&server_ptr->db, 1, user_setting_conn);
    cJSON_Delete(json_ptr);
    cJSON_Delete(result_json);
    return ;
}

