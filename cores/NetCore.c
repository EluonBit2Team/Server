#include "NetCore.h"

#define TRY for (int i = 0; i < 1; i++)
#define CONTI continue

// (워커스레드들이)할 일의 정보를 담으면, 동기화 기법(뮤텍스)을 고려해서 담는 함수.
bool enqueue_task(thread_pool_t* thread_pool, int req_client_fd, ring_buf *org_buf, int org_data_size)
{
    task_t new_task;
    if (ring_array(org_buf, new_task.buf) == false)
    {
        return false;
    }
    new_task.req_client_fd = req_client_fd;
    new_task.task_data_len = org_buf->msg_size;

    pthread_mutex_lock(&thread_pool->task_mutex);
    enqueue(&thread_pool->task_queue, (void*)&new_task);
    pthread_cond_signal(&thread_pool->task_cond);
    pthread_mutex_unlock(&thread_pool->task_mutex);
    return true;
}

// 워커스레드에서 할 일을 꺼낼때(des에 복사) 쓰는 함수.
bool deqeueu_and_get_task(thread_pool_t* thread_pool, task_t* des)
{
    pthread_mutex_lock(&thread_pool->task_mutex);
    if (dequeue(&thread_pool->task_queue, (void*)des) < 0)
    {
        pthread_mutex_unlock(&thread_pool->task_mutex);
        return false;
    }
    pthread_mutex_unlock(&thread_pool->task_mutex);
    return true;
}

// 워커스레드가 무한반 복할 루틴.
void* work_routine(void *ptr)
{
    epoll_net_core* server_ptr = (epoll_net_core *)ptr;
    thread_pool_t* thread_pool = &server_ptr->thread_pool;
    while (1) {
        // 큐에 할 일이 쌓일때까지 컨디션벨류를 이용해 대기
        pthread_mutex_lock(&thread_pool->task_mutex);
        while (is_empty(&thread_pool->task_queue) == true) {
            pthread_cond_wait(&thread_pool->task_cond, &thread_pool->task_mutex);
        }
        pthread_mutex_unlock(&thread_pool->task_mutex);

        task_t temp_task;
        // 할 일을 temp_task에 복사하고
        // 미리 설정해둔 서비스 배열로, 적합한 함수 포인터를 호출하여 처리
        if (deqeueu_and_get_task(thread_pool, &temp_task) == true)
        {
            int type = type_finder(temp_task.buf + HEADER_SIZE);
            if (type < 0)
            {
                printf("invalid type\n");
            }
            printf("type num:%d\n", type);
            server_ptr->function_array[type](server_ptr, &temp_task);
        }
    }
    return NULL;
}

// 스레드 풀 관련 초기화
void init_worker_thread(epoll_net_core* server_ptr, thread_pool_t* thread_pool_t_ptr)
{
    pthread_mutex_init(&thread_pool_t_ptr->task_mutex, NULL);
    pthread_cond_init(&thread_pool_t_ptr->task_cond, NULL);
    init_queue(&thread_pool_t_ptr->task_queue, sizeof(task_t));
    for (int i = 0; i < WOKER_THREAD_NUM; i++)
    {
        pthread_create(&thread_pool_t_ptr->worker_threads[i], NULL, work_routine, server_ptr);
    }
}

char* get_rear_send_buf_ptr(void_queue_t* vq)
{
    if (get_rear_data(vq) == NULL)
    {
        return NULL;
    }
    return ((send_buf_t*)get_rear_data(vq))->buf;
}

// todo : queue함수로 옮기기.
size_t get_rear_send_buf_size(void_queue_t* vq)
{
    //return *((size_t*)get_rear_data(vq));
    return ((send_buf_t*)get_rear_data(vq))->send_data_size;
}

// todo : 좀 더 일반적인 형태로. queueu를 받지 않고, serv랑 세션을 받게.
void reserve_send(void_queue_t* vq, char* send_org, int send_size)
{
    if (send_size > BUFF_SIZE)
    {
        return ;
    }
    send_buf_t temp_send_buf;
    // send_size는 int여야함.
    send_size += sizeof(send_size);
    temp_send_buf.send_data_size = send_size;

    memcpy(temp_send_buf.buf, (char*)&send_size, sizeof(send_size));
    memcpy(temp_send_buf.buf + sizeof(send_size), send_org, send_size);
    //write(STDOUT_FILENO, "enqueue:", 8); write(STDOUT_FILENO, temp_send_buf.buf, send_size); write(STDOUT_FILENO, "\n", 1);
    enqueue(vq, (void*)&temp_send_buf);
}

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
    const char* msg = NULL;
    cJSON* result_json = cJSON_CreateObject();
    client_session_t* now_session = NULL;
    conn_t* conn1 = NULL;
    conn_t* conn2 = NULL;
    MYSQL_RES *query_result = NULL;
    char SQL_buf[512];

    struct epoll_event temp_send_event;
    now_session = find_session_by_fd(&server_ptr->session_pool, task->req_client_fd);
    if (now_session == NULL)
    {
        msg = "session error";
        goto cleanup_and_respond;
    }
    temp_send_event.events = EPOLLOUT | EPOLLET;
    temp_send_event.data.fd = now_session->fd;

    cJSON* json_ptr = get_parsed_json(task->buf);
    if (json_ptr == NULL)
    {
        msg = "user send invalid json";
        goto cleanup_and_respond;
    }

    cJSON* name_ptr = cJSON_GetObjectItem(json_ptr, "id");
    if (name_ptr == NULL)
    {
        msg = "user send invalid json. Miss name";
        goto cleanup_and_respond;
    }

    cJSON* pw_ptr = cJSON_GetObjectItem(json_ptr, "pw");
    if (pw_ptr == NULL)
    {
        msg = "user send invalid json. Miss pw";
        goto cleanup_and_respond;
    }

    snprintf(SQL_buf, sizeof(SQL_buf), 
        "SELECT uid FROM user WHERE '%s' = user.login_id AND UNHEX(SHA2('%s', %d)) = user.password",
        cJSON_GetStringValue(name_ptr), cJSON_GetStringValue(pw_ptr), SHA2_HASH_LENGTH);

    conn1 = get_conn(&server_ptr->db.pools[USER_SETTING_DB_IDX]);
    if (mysql_query(conn1->conn, SQL_buf)) {
        fprintf(stderr, "login query fail: %s\n", mysql_error(conn1->conn));
        msg = "login query fail";
        goto cleanup_and_respond;
    }

    query_result = mysql_store_result(conn1->conn);
    if (query_result == NULL) {
        fprintf(stderr, "mysql_store_result failed: %s\n", mysql_error(conn1->conn));
        msg = "mysql_store_result failed";
        goto cleanup_and_respond;
    }

    int uid = -1;
    MYSQL_ROW row;
    if ((row = mysql_fetch_row(query_result))) {
        uid = atoi(row[0]);
    }
    else
    {
        msg = "Invalid ID or PW";
        goto cleanup_and_respond;
    }

    insert(&server_ptr->fd_to_uid_hash, task->req_client_fd, uid);
    insert(&server_ptr->uid_to_fd_hash, uid, task->req_client_fd);
    printf("%d user login\n", find(&server_ptr->fd_to_uid_hash, task->req_client_fd));
    type = 2;
    msg = "LOGIN SUCCESS";

    // 로그인 성공시 DB에 로그 저장
    snprintf(SQL_buf, sizeof(SQL_buf), 
        "INSERT INTO client_log (uid, login_time) VALUES (%d, NOW())", find(&server_ptr->fd_to_uid_hash, task->req_client_fd));

    conn2 = get_conn(&server_ptr->db.pools[LOG_DB_IDX]);
    if (mysql_query(conn2->conn, SQL_buf)) {
        fprintf(stderr, "login_time_log query fail: %s\n", mysql_error(conn2->conn));
        msg = "login query fail";
        goto cleanup_and_respond;
    }

cleanup_and_respond:
    printf("%d %s\n", task->req_client_fd, msg);
    cJSON_AddNumberToObject(result_json, "type", type);
    cJSON_AddStringToObject(result_json, "msg", msg);
    cJSON_AddStringToObject(result_json, "id", cJSON_GetStringValue(name_ptr));
    reserve_send(&now_session->send_bufs, cJSON_Print(result_json), strlen(cJSON_Print(result_json)));
    if (epoll_ctl(server_ptr->epoll_fd, EPOLL_CTL_MOD, now_session->fd, &temp_send_event) == -1) {
        perror("epoll_ctl: add");
    }
    if ((conn1 != NULL) || (conn2 != NULL))
    {
        release_conn(&server_ptr->db.pools[USER_SETTING_DB_IDX], conn1);
        release_conn(&server_ptr->db.pools[LOG_DB_IDX], conn2);
    }
    if (query_result != NULL)
    {
        mysql_free_result(query_result);
    }
    cJSON_Delete(json_ptr);
    return ;
}

void signup_service(epoll_net_core* server_ptr, task_t* task) {
    printf("signup_service\n");
    int type = 100;
    const char* msg = NULL;
    cJSON* result_json = cJSON_CreateObject();
    client_session_t* now_session = NULL;
    conn_t* conn1 = NULL;
    conn_t* conn2 = NULL;
    MYSQL_RES *query_result = NULL;
    MYSQL_ROW row;
    char SQL_buf[1024];

    conn1 = get_conn(&server_ptr->db.pools[USER_REQUEST_DB_IDX]);
    conn2 = get_conn(&server_ptr->db.pools[USER_SETTING_DB_IDX]);
    struct epoll_event temp_send_event;
    now_session = find_session_by_fd(&server_ptr->session_pool, task->req_client_fd);

    if (now_session == NULL)
    {
        printf("now_session NULL\n");
        goto cleanup_and_respond;
    }
    temp_send_event.events = EPOLLOUT | EPOLLET;
    temp_send_event.data.fd = now_session->fd;

    cJSON* json_ptr = get_parsed_json(task->buf);
    if (json_ptr == NULL)
    {
        msg = "user send invalid json";
        goto cleanup_and_respond;
    }
    cJSON* name_ptr = cJSON_GetObjectItem(json_ptr, "name");
    if (name_ptr == NULL || cJSON_GetStringValue(name_ptr)[0] == '\0')
    {
        msg = "user send invalid json. Miss name";
        goto cleanup_and_respond;
    }
    cJSON* id_ptr = cJSON_GetObjectItem(json_ptr, "id");
    if (id_ptr == NULL || cJSON_GetStringValue(name_ptr)[0] == '\0')
    {
        msg = "user send invalid json. Miss id";
        goto cleanup_and_respond;
    }
    cJSON* pw_ptr = cJSON_GetObjectItem(json_ptr, "pw");
    if (pw_ptr == NULL || cJSON_GetStringValue(name_ptr)[0] == '\0')
    {
        msg = "user send invalid json. Miss pw";
        goto cleanup_and_respond;
    }
    cJSON* phone_ptr = cJSON_GetObjectItem(json_ptr, "phone");
    if (phone_ptr == NULL || cJSON_GetStringValue(name_ptr)[0] == '\0')
    {
        msg = "user send invalid json. Miss phone";
        goto cleanup_and_respond;
    }
    cJSON* email_ptr = cJSON_GetObjectItem(json_ptr, "email");
    if (email_ptr == NULL || cJSON_GetStringValue(name_ptr)[0] == '\0')
    {
        msg = "user send invalid json. Miss email";
        goto cleanup_and_respond;
    }
    cJSON* dept_ptr = cJSON_GetObjectItem(json_ptr, "dept");
    if (dept_ptr == NULL)
    {
        msg = "user send invalid json. Miss dept";
        goto cleanup_and_respond;
    }
    cJSON* pos_ptr = cJSON_GetObjectItem(json_ptr, "pos");
    if (pos_ptr == NULL)
    {
        msg = "user send invalid json. Miss pos";
        goto cleanup_and_respond;
    }

    snprintf(SQL_buf, sizeof(SQL_buf), "SELECT COUNT(login_id) FROM user WHERE login_id = '%s'", cJSON_GetStringValue(id_ptr));
    if (mysql_query(conn2->conn, SQL_buf)) {
        fprintf(stderr, "SELECT failed: %s\n", mysql_error(conn2->conn));
        msg = "SELECT failed";
        goto cleanup_and_respond;
    }

    query_result = mysql_store_result(conn2->conn);
    if (query_result == NULL) {
        fprintf(stderr, "mysql_store_result failed: %s\n", mysql_error(conn2->conn));
        msg = "mysql_store_result failed";
        goto cleanup_and_respond;
    }

    row = mysql_fetch_row(query_result);
    if (row && atoi(row[0]) > 0) {
        msg = "login_id already exists.";
        goto cleanup_and_respond;
    }
    mysql_free_result(query_result);
    query_result = NULL;
    
    snprintf(SQL_buf, sizeof(SQL_buf), 
             "INSERT INTO signup_req (login_id, password, name, phone, email, deptno, position) VALUES ('%s', UNHEX(SHA2('%s',%d)), '%s', '%s', '%s', '%d', '%d')",
             cJSON_GetStringValue(id_ptr), cJSON_GetStringValue(pw_ptr), SHA2_HASH_LENGTH, cJSON_GetStringValue(name_ptr),
             cJSON_GetStringValue(phone_ptr), cJSON_GetStringValue(email_ptr), cJSON_GetNumberValue(dept_ptr),cJSON_GetNumberValue(pos_ptr));
    if (mysql_query(conn1->conn, SQL_buf)) {
        msg = "INSERT failed";
        goto cleanup_and_respond;
    }
    type = 1;
    msg = "SIGNUP SUCCESS";
    goto cleanup_and_respond;

cleanup_and_respond:
    printf("%d %s\n", task->req_client_fd, msg);
    cJSON_AddNumberToObject(result_json, "type", type);
    cJSON_AddStringToObject(result_json, "msg", msg);
    reserve_send(&now_session->send_bufs, cJSON_Print(result_json), strlen(cJSON_Print(result_json)));

    if (epoll_ctl(server_ptr->epoll_fd, EPOLL_CTL_MOD, now_session->fd, &temp_send_event) == -1) {
        perror("epoll_ctl: add");
    }
    if ((conn1 != NULL) || (conn2 != NULL))
    {
        release_conn(&server_ptr->db.pools[USER_REQUEST_DB_IDX], conn1);
        release_conn(&server_ptr->db.pools[USER_SETTING_DB_IDX], conn2);
    }
    if (query_result != NULL)
    {
        mysql_free_result(query_result);
    }
    cJSON_Delete(json_ptr);
    cJSON_Delete(result_json);
    return ;
}

void make_group_service(epoll_net_core* server_ptr, task_t* task)
{
    printf("make_group_service\n");
    int type = 100;
    const char* msg = NULL;
    cJSON* result_json = cJSON_CreateObject();
    client_session_t* now_session = NULL;
    conn_t* conn1 = NULL;
    conn_t* conn2 = NULL;
    MYSQL_RES *query_result = NULL;
    MYSQL_ROW row;
    char SQL_buf[512];

    conn1 = get_conn(&server_ptr->db.pools[USER_SETTING_DB_IDX]);
    conn2 = get_conn(&server_ptr->db.pools[USER_REQUEST_DB_IDX]);

    struct epoll_event temp_send_event;
    now_session = find_session_by_fd(&server_ptr->session_pool, task->req_client_fd);
    if (now_session == NULL)
    {
        msg = "session error";
        goto cleanup_and_respond;
    }
    temp_send_event.events = EPOLLOUT | EPOLLET;
    temp_send_event.data.fd = now_session->fd;

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
    if (id_ptr == NULL || cJSON_GetStringValue(groupname_ptr)[0] == '\0')
    {
        msg = "user send invalid json. Miss uid";
        goto cleanup_and_respond;
    }

    cJSON* message_ptr = cJSON_GetObjectItem(json_ptr, "message");
    if (message_ptr == NULL || cJSON_GetStringValue(groupname_ptr)[0] == '\0')
    {
        msg = "user send invalid json. Miss message";
        goto cleanup_and_respond;
    }

    snprintf(SQL_buf, sizeof(SQL_buf), 
        "SELECT uid FROM user AS u WHERE '%s' = login_id ",
        cJSON_GetStringValue(id_ptr));

    if (mysql_query(conn1->conn, SQL_buf)) {
        fprintf(stderr, "SELECT failed: %s\n", mysql_error(conn1->conn));
        msg = "DB error";
        goto cleanup_and_respond;
    }

    query_result = mysql_store_result(conn1->conn);
    if (query_result == NULL) {
        fprintf(stderr, "mysql_store_result failed: %s\n", mysql_error(conn1->conn));
        msg = "DB error";
        goto cleanup_and_respond;
    }
    
    row = mysql_fetch_row(query_result);
    if (row == NULL) {
        fprintf(stderr, "No data fetched\n");
        msg = "No data fetched";
        goto cleanup_and_respond;
    }

    int uid_value = atoi(row[0]);
    mysql_free_result(query_result);
    query_result = NULL;

    snprintf(SQL_buf, sizeof(SQL_buf), 
        "INSERT INTO group_req (groupname, uid) VALUES ('%s', '%d')",
        cJSON_GetStringValue(groupname_ptr), uid_value);

    if (mysql_query(conn2->conn, SQL_buf)) {
        fprintf(stderr, "INSERT failed: %s\n", mysql_error(conn2->conn));
        msg = "INSERT failed";
        goto cleanup_and_respond;
    }
    
    type = 4;
    msg = "Make Group Success";
    goto cleanup_and_respond;

cleanup_and_respond:
    printf("%d %s", task->req_client_fd, msg);
    // cJSON_AddNumberToObject(result_json, "type", type);
    // cJSON_AddStringToObject(result_json, "msg", msg);
    // char *response_str = cJSON_Print(result_json);
    // reserve_send(&now_session->send_bufs, response_str, strlen(response_str));
    // free(response_str);
    if (epoll_ctl(server_ptr->epoll_fd, EPOLL_CTL_MOD, now_session->fd, &temp_send_event) == -1) {
        perror("epoll_ctl: add");
    }
    if ((conn1 != NULL) || (conn2 != NULL))
    {
        release_conn(&server_ptr->db.pools[USER_SETTING_DB_IDX], conn1);
        release_conn(&server_ptr->db.pools[USER_REQUEST_DB_IDX], conn2);
    }

    if (query_result != NULL)
    {
        mysql_free_result(query_result);
    }
    cJSON_Delete(json_ptr);
    cJSON_Delete(result_json);
    return ;
}

void user_list_service(epoll_net_core* server_ptr, task_t* task) {
    printf("user_list_service\n");
    int type = 100;
    const char* msg = NULL;
    cJSON* result_json = cJSON_CreateObject();
    client_session_t* now_session = NULL;
    conn_t* conn = NULL;
    MYSQL_RES *query_result = NULL;
    MYSQL_ROW row;
    char SQL_buf[1024];

    cJSON* json_ptr = get_parsed_json(task->buf);
    if (json_ptr == NULL)
    {
        msg = "user send invalid json";
        goto cleanup_and_respond;
    }

    cJSON* page_ptr = cJSON_GetObjectItem(json_ptr, "page");
    if (page_ptr == NULL || !cJSON_IsNumber(page_ptr))
    {
        msg = "user send invalid json. Miss page";
        goto cleanup_and_respond;
    }

    int page = cJSON_GetNumberValue(page_ptr);

    int limit = 10;
    int offset = (page - 1) * limit;

    struct epoll_event temp_send_event;
    now_session = find_session_by_fd(&server_ptr->session_pool, task->req_client_fd);
    if (now_session == NULL)
    {
        msg = "session error";
        goto cleanup_and_respond;
    }
    temp_send_event.events = EPOLLOUT | EPOLLET;
    temp_send_event.data.fd = now_session->fd;

    snprintf(SQL_buf, sizeof(SQL_buf), 
        "SELECT u.name, jp.position_name, d.dept_name FROM user u LEFT JOIN dept d ON u.did = d.did LEFT JOIN job_position jp ON jp.pid = u.position LIMIT %d OFFSET %d", limit, offset);

    conn = get_conn(&server_ptr->db.pools[USER_SETTING_DB_IDX]);
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
    
    cJSON* users_array = cJSON_CreateArray();
    while ((row = mysql_fetch_row(query_result))) {
        cJSON* user_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(user_obj, "name", row[0]);
        cJSON_AddStringToObject(user_obj, "position", row[1]);
        cJSON_AddStringToObject(user_obj, "dept_name", row[2]);
        cJSON_AddItemToArray(users_array, user_obj);
    }

    if (cJSON_GetArraySize(users_array) == 0) {
        msg = "No data fetched";
        goto cleanup_and_respond;
    }

    type = 5;
    msg = "User List Send Success";

cleanup_and_respond:
    printf("%d %s", task->req_client_fd, msg);
    cJSON_AddNumberToObject(result_json, "type", type);
    cJSON_AddStringToObject(result_json, "msg", msg);
    cJSON_AddItemToObject(result_json, "users", users_array);
    char *response_str = cJSON_Print(result_json);
    reserve_send(&now_session->send_bufs, response_str, strlen(response_str));
    free(response_str);
    if (epoll_ctl(server_ptr->epoll_fd, EPOLL_CTL_MOD, now_session->fd, &temp_send_event) == -1) {
        perror("epoll_ctl: add");
    }
    if (conn != NULL)
    {
        release_conn(&server_ptr->db.pools[USER_SETTING_DB_IDX], conn);
    }
    if (query_result != NULL)
    {
        mysql_free_result(query_result);
    }
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

    snprintf(SQL_buf, sizeof(SQL_buf), "SELECT cg.groupname FROM chat_group AS cg");

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
    free(response_str);
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
    printf("make_group_service\n");
    int type = 100;
    const char* msg = NULL;
    cJSON* result_json = cJSON_CreateObject();
    client_session_t* now_session = NULL;
    conn_t* conn1 = NULL;
    conn_t* conn2 = NULL;
    MYSQL_RES *query_result = NULL;
    MYSQL_ROW row;
    char SQL_buf[512];
    
    conn1 = get_conn(&server_ptr->db.pools[CHAT_GROUP_DB_IDX]);
    conn2 = get_conn(&server_ptr->db.pools[USER_SETTING_DB_IDX]);
    
    int host_uid = find(&server_ptr->fd_to_uid_hash, task->req_client_fd);

    struct epoll_event temp_send_event;
    now_session = find_session_by_fd(&server_ptr->session_pool, task->req_client_fd);
    if (now_session == NULL)
    {
        msg = "session error";
        goto cleanup_and_respond;
    }
    temp_send_event.events = EPOLLOUT | EPOLLET;
    temp_send_event.data.fd = now_session->fd;

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
        "SELECT is_host FROM group_member WHERE '%d' = uid",
        host_uid);

    if (mysql_query(conn1->conn, SQL_buf)) {
        fprintf(stderr, "SELECT failed: %s\n", mysql_error(conn1->conn));
        msg = "DB error";
        goto cleanup_and_respond;
    }

    query_result = mysql_store_result(conn1->conn);
    if (query_result == NULL) {
        fprintf(stderr, "mysql_store_result failed: %s\n", mysql_error(conn1->conn));
        msg = "DB error";
        goto cleanup_and_respond;
    }
    
    row = mysql_fetch_row(query_result);
    if (row == NULL) {
        fprintf(stderr, "No data fetched\n");
        msg = "No data fetched";
        goto cleanup_and_respond;
    }

    int is_host_value = atoi(row[0]);
    if (!is_host_value) {
        msg = "You are not host!!!!";
        goto cleanup_and_respond;
    }

    mysql_free_result(query_result);
    query_result = NULL;

    snprintf(SQL_buf, sizeof(SQL_buf), 
        "SELECT gid FROM group WHERE '%s' = groupname",
        host_uid);

    if (mysql_query(conn1->conn, SQL_buf)) {
        fprintf(stderr, "SELECT failed: %s\n", mysql_error(conn1->conn));
        msg = "DB error";
        goto cleanup_and_respond;
    }

    query_result = mysql_store_result(conn1->conn);
    if (query_result == NULL) {
        fprintf(stderr, "mysql_store_result failed: %s\n", mysql_error(conn1->conn));
        msg = "DB error";
        goto cleanup_and_respond;
    }
    
    row = mysql_fetch_row(query_result);
    if (row == NULL) {
        fprintf(stderr, "No data fetched\n");
        msg = "No data fetched";
        goto cleanup_and_respond;
    }

    int gid_value = atoi(row[0]);
    mysql_free_result(query_result);
    query_result = NULL;

    int array_size = cJSON_GetArraySize(username_ptr);
    for (int i = 0; i < array_size; i++) {
        cJSON* user_item = cJSON_GetArrayItem(username_ptr, i);
        if (user_item == NULL || cJSON_GetStringValue(user_item)[0] == '\0') {
            msg = "Invalid JSON: Empty username in users list";
            goto cleanup_and_respond;
        }

        snprintf(SQL_buf, sizeof(SQL_buf), "SELECT uid FROM user WHERE username = '%s'", cJSON_GetStringValue(user_item));
        if (mysql_query(conn2->conn, SQL_buf)) {
            fprintf(stderr, "SELECT failed: %s\n", mysql_error(conn2->conn));
            msg = "DB error";
            goto cleanup_and_respond;
        }

        query_result = mysql_store_result(conn2->conn);
        if (query_result == NULL) {
            fprintf(stderr, "mysql_store_result failed: %s\n", mysql_error(conn2->conn));
            msg = "DB error";
            goto cleanup_and_respond;
        }

        row = mysql_fetch_row(query_result);
        if (row == NULL) {
            fprintf(stderr, "No data fetched for user: %s\n", cJSON_GetStringValue(user_item));
            msg = "No data fetched";
            mysql_free_result(query_result);
            goto cleanup_and_respond;
        }

        int useruid_value = atoi(row[0]);
        mysql_free_result(query_result);
        query_result = NULL;

        snprintf(SQL_buf, sizeof(SQL_buf), "INSERT INTO group_member (uid, gid) VALUES ('%d', '%d')", useruid_value, gid_value);
        if (mysql_query(conn1->conn, SQL_buf)) {
            fprintf(stderr, "INSERT failed: %s\n", mysql_error(conn1->conn));
            msg = "INSERT failed";
            goto cleanup_and_respond;
        }
    }
    type = 7;
    msg = "Add Group Member Success";
    goto cleanup_and_respond;

cleanup_and_respond:
    printf("%d %s", task->req_client_fd, msg);
    cJSON_AddNumberToObject(result_json, "type", type);
    cJSON_AddStringToObject(result_json, "msg", msg);
    char *response_str = cJSON_Print(result_json);
    reserve_send(&now_session->send_bufs, response_str, strlen(response_str));
    free(response_str);
    if (epoll_ctl(server_ptr->epoll_fd, EPOLL_CTL_MOD, now_session->fd, &temp_send_event) == -1) {
        perror("epoll_ctl: add");
    }
    if ((conn1 != NULL) || (conn2 != NULL))
    {
        release_conn(&server_ptr->db.pools[USER_SETTING_DB_IDX], conn1);
        release_conn(&server_ptr->db.pools[USER_REQUEST_DB_IDX], conn2);
    }

    if (query_result != NULL)
    {
        mysql_free_result(query_result);
    }
    cJSON_Delete(json_ptr);
    cJSON_Delete(result_json);
    return ;
}

void set_sock_nonblocking_mode(int sockFd) {
    int flag = fcntl(sockFd, F_GETFL, 0);
    fcntl(sockFd, F_SETFL, flag | O_NONBLOCK);
}

// 서버 초기화
bool init_server(epoll_net_core* server_ptr) {
    if (init_mariadb(&server_ptr->db) == false)
    {
        printf("DB conn Failse\n");
        return false;
    }
    // 세션 초기화
    init_session_pool(&server_ptr->session_pool, MAX_CLIENT_NUM);
    init_hash_map(&server_ptr->fd_to_uid_hash, MAX_CLIENT_NUM);
    init_hash_map(&server_ptr->uid_to_fd_hash, MAX_CLIENT_NUM);

    // 서버 주소 설정
    server_ptr->is_run = false;
    server_ptr->listen_addr.sin_family = AF_INET;
    server_ptr->listen_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_ptr->listen_addr.sin_port = htons(PORT);

    // 서비스 함수 초기화 및 추가
    for (int i = 0; i < SERVICE_FUNC_NUM; i++)
    {
        server_ptr->function_array[i] = NULL;
    }
    server_ptr->function_array[ECHO_SERVICE_FUNC] = echo_service;
    server_ptr->function_array[LOGIN_SERV_FUNC] = login_service;
    server_ptr->function_array[SIGNUP_SERV_FUNC] = signup_service;
    server_ptr->function_array[MAKE_GROUP_SERV_FUNC] = make_group_service;
    server_ptr->function_array[USER_LIST_SERV_FUNC] = user_list_service;
    server_ptr->function_array[GROUP_LIST_SERV_FUNC] = group_list_service;
    server_ptr->function_array[ADD_MEMBER_SERV_FUNC] = add_member_service;

    // 리슨소켓 생성
    server_ptr->listen_fd = socket(PF_INET, SOCK_STREAM, 0);
    if (server_ptr->listen_fd < 0)
    {
        printf("listen sock assignment error: %d\n", errno);
        return false;
    }
    int opt = 1;
    setsockopt(server_ptr->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    set_sock_nonblocking_mode(server_ptr->listen_fd);
    return true;
}

// accept시 동작 처리 함수
int accept_client(epoll_net_core* server_ptr) {
    struct epoll_event temp_event;
    struct sockaddr_in client_addr;
    socklen_t client_addr_size = client_addr_size = sizeof(client_addr);
    int client_sock = accept(server_ptr->listen_fd, (struct sockaddr*)&(client_addr), &client_addr_size);
    if (client_sock < 0) {
        printf("accept error: %d\n", errno);
    }
    set_sock_nonblocking_mode(client_sock);

    // 세션 초기화
    client_session_t* sesseion_ptr = assign_session(&server_ptr->session_pool, client_sock);
    if (sesseion_ptr == NULL)
    {
        printf("assign fail\n");
        return -1;
    }
    temp_event.data.fd = client_sock;
    // ✨ 엣지트리거방식의(EPOLLIN) 입력 이벤트 대기 설정(EPOLLET)
    temp_event.events = EPOLLIN | EPOLLET;
    epoll_ctl(server_ptr->epoll_fd, EPOLL_CTL_ADD, client_sock, &temp_event);
    printf("accept %d client, session id : %ld \n", client_sock, sesseion_ptr->session_idx);
}

void disconnect_client(epoll_net_core* server_ptr, int client_fd)
{
    conn_t* conn = NULL;
    char SQL_buf[512];
    conn = get_conn(&server_ptr->db.pools[LOG_DB_IDX]);

    snprintf(SQL_buf, sizeof(SQL_buf),"UPDATE client_log SET logout_time = NOW() WHERE logout_time IS NULL AND uid = %d",find(&server_ptr->fd_to_uid_hash, client_fd));
    if (mysql_query(conn->conn, SQL_buf)) {
        fprintf(stderr, "login query fail: %s\n", mysql_error(conn->conn));
    }
    epoll_ctl(server_ptr->epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
    client_session_t* session_ptr = find_session_by_fd(&server_ptr->session_pool, client_fd);
    if (session_ptr == NULL)
    {
        printf("fd and session mismatch in disconnect_client\n");
    }
    else
    {
        close_session(&server_ptr->session_pool, session_ptr);
    }
    if (conn != NULL)
    {
        release_conn(&server_ptr->db.pools[LOG_DB_IDX], conn);
    }

    close(client_fd);
    printf("disconnect:%d\n", client_fd);
}

int run_server(epoll_net_core* server_ptr) {
    server_ptr->is_run = true;

    struct epoll_event temp_epoll_event;
    server_ptr->epoll_fd = epoll_create1(0);
    if (server_ptr->epoll_fd < 0)
    {
        printf("epoll_fd Error : %d\n", errno);
    }
    server_ptr->epoll_events = malloc(sizeof(struct epoll_event) * EPOLL_SIZE);

    // 서버 run시 워커스레드 생성하고 돌리기 
    init_worker_thread(server_ptr, &server_ptr->thread_pool);

    int rt_val = bind(server_ptr->listen_fd, 
        (struct sockaddr*) &server_ptr->listen_addr, 
        sizeof(server_ptr->listen_addr));
    if (rt_val < 0) {
        printf("bind Error : %d\n", errno);
    }
    rt_val = listen(server_ptr->listen_fd, SOMAXCONN);
    if (rt_val < 0) {
        printf("listen Error : %d\n", errno);
    }
    set_sock_nonblocking_mode(server_ptr->listen_fd);

    // 리슨 소켓 이벤트 등록
    temp_epoll_event.events = EPOLLIN;
    temp_epoll_event.data.fd = server_ptr->listen_fd;
    rt_val = epoll_ctl(server_ptr->epoll_fd, EPOLL_CTL_ADD, server_ptr->listen_fd, &temp_epoll_event);
    if (rt_val < 0) {
        printf("epoll_ctl Error : %d\n", errno);
    }

    // 메인 스레드(main함수에서 run_server()까지 호출한 메인 흐름)가 epoll_wait로 io완료 대기
    while (server_ptr->is_run == true) {
        int occured_event_cnt = epoll_wait(
            server_ptr->epoll_fd, server_ptr->epoll_events, 
            EPOLL_SIZE, -1);
        if (occured_event_cnt < 0) {
            printf("epoll_wait Error : %d\n", errno);
        }
        
        for (int i = 0; i < occured_event_cnt; i++) {
            // accept 이벤트시
            if (server_ptr->epoll_events[i].data.fd == server_ptr->listen_fd) {
                accept_client(server_ptr);
            }
            // 유저로부터 데이터가 와서, read할 수 있는 이벤트 발생시
            else if (server_ptr->epoll_events[i].events & EPOLLIN) {
                int client_fd = server_ptr->epoll_events[i].data.fd;
                // session 변경함.
                client_session_t* s_ptr = find_session_by_fd(&server_ptr->session_pool, client_fd);
                if (s_ptr == NULL)
                {
                    printf("%d session is invalid\n", client_fd);
                    continue;
                }
                
                int input_size = ring_read(&s_ptr->recv_bufs, client_fd);
                if (input_size <= 0) {
                    disconnect_client(server_ptr, client_fd);
                    continue;
                }
                while(1) {
                    if (enqueue_task(&server_ptr->thread_pool, client_fd, &s_ptr->recv_bufs, input_size) == false)
                    {
                        break;
                    }
                }
            }
            // 이벤트에 입력된 fd의 send버퍼가 비어서, send가능할시 발생하는 이벤트
            else if (server_ptr->epoll_events[i].events & EPOLLOUT) {
                send_buf_t temp_send_buf;
                int client_fd = server_ptr->epoll_events[i].data.fd;
                // send버퍼가 비어있으므로, send 성공이 보장되므로 send수행
                client_session_t* s_ptr = find_session_by_fd(&server_ptr->session_pool, client_fd);
                if (s_ptr == NULL)
                {
                    printf("%d session is invalid in send\n", client_fd);
                    continue;
                }
                if (is_empty(&s_ptr->send_bufs) == true)
                {
                    continue ;
                }
                //printf("send session id:%ld, fd:%d\n", s_ptr->session_idx, s_ptr->fd);
                char* send_buf_ptr = get_rear_send_buf_ptr(&s_ptr->send_bufs);
                if (send_buf_ptr == NULL)
                {
                    continue ;
                }

                size_t sent = send(client_fd, send_buf_ptr, get_rear_send_buf_size(&s_ptr->send_bufs), 0);
                write(STDOUT_FILENO, "SEND:", 5); write(STDOUT_FILENO, send_buf_ptr, get_rear_send_buf_size(&s_ptr->send_bufs)); write(STDOUT_FILENO, "\n", 1);
                if (sent < 0) {
                    perror("send");
                    close(server_ptr->epoll_events[i].data.fd);
                }
                dequeue(&s_ptr->send_bufs, NULL);
                
                // send할 때 이벤트를 변경(EPOLL_CTL_MOD)해서 보내는 이벤트로 바꿨으니
                // 다시 통신을 받는 이벤트로 변경하여 유저의 입력을 대기.
                struct epoll_event temp_event;
                temp_event.events = EPOLLIN | EPOLLET;
                temp_event.data.fd = server_ptr->epoll_events[i].data.fd;
                if (epoll_ctl(server_ptr->epoll_fd, EPOLL_CTL_MOD, server_ptr->epoll_events[i].data.fd, &temp_event) == -1) {
                    perror("epoll_ctl: del");
                    close(server_ptr->epoll_events[i].data.fd);
                }
            }
            else {
                printf("?\n");
            }
        }
    }
}

void down_server(epoll_net_core* server_ptr) {
    printf("down server\n");
    server_ptr->is_run = false;
    close(server_ptr->listen_fd);
    close(server_ptr->epoll_fd);
    free(server_ptr->epoll_events);
    for (int i = 0; i < WOKER_THREAD_NUM; i++) {
       pthread_join(server_ptr->thread_pool.worker_threads[i], NULL);
    }
    close_all_sessions(&server_ptr->session_pool);
    clear_hash_map(&server_ptr->fd_to_uid_hash);
    clear_hash_map(&server_ptr->uid_to_fd_hash);
    close_mariadb(&server_ptr->db);
}