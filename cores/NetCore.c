#include "NetCore.h"

// (워커스레드들이)할 일의 정보를 담으면, 동기화 기법(뮤텍스)을 고려해서 담는 함수.
bool enqueue_task(thread_pool_t* thread_pool, int req_client_fd, ring_buf *org_buf, int org_data_size)
{
    task new_task;
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
bool deqeueu_and_get_task(thread_pool_t* thread_pool, task* des)
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

        task temp_task;
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
    init_queue(&thread_pool_t_ptr->task_queue, sizeof(task));
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

size_t get_rear_send_buf_size(void_queue_t* vq)
{
    return *((size_t*)get_rear_data(vq));
}

void reserve_send(void_queue_t* vq, char* send_org, size_t send_size)
{
    if (send_size > BUFF_SIZE)
    {
        return ;
    }
    send_buf_t temp_send_buf;
    temp_send_buf.send_data_size = send_size;
    memcpy(temp_send_buf.buf, send_org, send_size);
    enqueue(vq, (void*)&temp_send_buf);
}

// ✨ 서비스 함수. 이런 형태의 함수들을 추가하여 서비스 추가. ✨
void echo_service(epoll_net_core* server_ptr, task* task) {
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

void login_service(epoll_net_core* server_ptr, task* task) {
    printf("login_service\n");
    cJSON* json_ptr = get_parsed_json(task->buf);
    cJSON* name_ptr = cJSON_GetObjectItem(json_ptr, "name");
    if (cJSON_IsString(name_ptr) == true)
    {
        printf("name: %s\n", name_ptr->valuestring);
    }
    cJSON* pw_ptr = cJSON_GetObjectItem(json_ptr, "pw");
    if (cJSON_IsString(name_ptr) == true)
    {
        printf("pw: %s\n", name_ptr->valuestring);
    }
    
    cJSON_Delete(json_ptr);
}

void signup_service(epoll_net_core* server_ptr, task* task) {
    printf("signup_service\n");
    conn_t* conn = get_conn(&server_ptr->db.pools[USER_REQUEST_DB_IDX]);
    printf("connection success\n");
    char * msg = NULL;
    int type = 100;
    char query[1024];
    MYSQL_RES *query_result;
    MYSQL_ROW row;
    client_session_t* now_session = NULL;
    struct epoll_event temp_send_event;
    now_session = find_session_by_fd(&server_ptr->session_pool, task->req_client_fd);
    if (now_session == NULL)
    {
        printf("now_session NULL\n");
    }
    temp_send_event.events = EPOLLOUT | EPOLLET;
    temp_send_event.data.fd = now_session->fd;

    cJSON* result_json = cJSON_CreateObject();
    cJSON* json_ptr = get_parsed_json(task->buf);
    if (json_ptr == NULL)
    {
        msg = "user send invalid json";
        goto cleanup_and_respond;
    }
    cJSON* name_ptr = cJSON_GetObjectItem(json_ptr, "name");
    printf("%s %d\n",&json_ptr,strlen(&json_ptr));
    if (json_ptr == NULL || strlen(json_ptr) == 0)
    {
        msg = "name passing error";
        goto cleanup_and_respond;
    }
    cJSON* id_ptr = cJSON_GetObjectItem(json_ptr, "id");
    if (json_ptr == NULL || strlen(json_ptr) == 0)
    {
        msg = "id passing error";
        goto cleanup_and_respond;
    }
    cJSON* pw_ptr = cJSON_GetObjectItem(json_ptr, "pw");
    if (json_ptr == NULL || strlen(json_ptr) == 0)
    {
        msg = "pw passing error";
        goto cleanup_and_respond;
    }
    cJSON* phone_ptr = cJSON_GetObjectItem(json_ptr, "phone");
    if (json_ptr == NULL || strlen(json_ptr) == 0)
    {
        msg = "phone passing error";
        goto cleanup_and_respond;
    }
    cJSON* email_ptr = cJSON_GetObjectItem(json_ptr, "email");
    if (json_ptr == NULL || strlen(json_ptr) == 0)
    {
        msg = "email passing error";
        goto cleanup_and_respond;
    }
    cJSON* dept_ptr = cJSON_GetObjectItem(json_ptr, "dept");
    if (json_ptr == NULL || strlen(json_ptr) == 0)
    {
        msg = "dept passing error";
        goto cleanup_and_respond;
    }
    cJSON* pos_ptr = cJSON_GetObjectItem(json_ptr, "pos");
    if (json_ptr == NULL || strlen(json_ptr) == 0)
    {
        msg = "pos passing error";
        goto cleanup_and_respond;
    }

    snprintf(query, sizeof(query), "SELECT COUNT(*) FROM signup_req WHERE login_id = '%s'", cJSON_GetStringValue(id_ptr));
    if (mysql_query(conn->conn, query)) {
        fprintf(stderr, "SELECT failed: %s\n", mysql_error(conn->conn));
        msg = "SELECT failed";
        goto cleanup_and_respond;
    }

    query_result = mysql_store_result(conn->conn);
    if (query_result == NULL) {
        fprintf(stderr, "mysql_store_result failed: %s\n", mysql_error(conn->conn));
        msg = "mysql_store_result failed";
        goto cleanup_and_respond;
    }

    row = mysql_fetch_row(query_result);
    if (row && atoi(row[0]) > 0) {
        msg = "login_id already exists.";
        goto cleanup_and_respond;
    }
    mysql_free_result(query_result);
    
    snprintf(query, sizeof(query), 
             "INSERT INTO signup_req (login_id, password, name, phone, email) VALUES ('%s', UNHEX(SHA2('%s',%d)), '%s', '%s', '%s')",
             cJSON_GetStringValue(id_ptr), cJSON_GetStringValue(pw_ptr), SHA2_HASH_LENGTH, cJSON_GetStringValue(name_ptr), cJSON_GetStringValue(phone_ptr), cJSON_GetStringValue(email_ptr));

    if (mysql_query(conn->conn, query)) {
        msg = "INSERT failed";
        goto cleanup_and_respond;
    }

    type = 101;
    msg = "SIGNUP SUCCESS";
    goto cleanup_and_respond;

cleanup_and_respond:
    printf("%d %s", task->req_client_fd, msg);
    cJSON_AddNumberToObject(result_json, "type", type);
    cJSON_AddStringToObject(result_json, "msg", msg);
    reserve_send(&now_session->send_bufs, cJSON_Print(result_json), strlen(cJSON_Print(result_json)));
    if (epoll_ctl(server_ptr->epoll_fd, EPOLL_CTL_MOD, now_session->fd, &temp_send_event) == -1) {
        perror("epoll_ctl: add");
    }
    cJSON_Delete(json_ptr);
    cJSON_Delete(result_json);
    if (conn != NULL)
    {
        release_conn(&server_ptr->db.pools[USER_REQUEST_DB_IDX], conn);
    }
    if (query_result != NULL)
    {
        mysql_free_result(query_result);
    }
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
                if (input_size == 0) {
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

                char* send_buf_ptr = get_rear_send_buf_ptr(&s_ptr->send_bufs);
                if (send_buf_ptr == NULL)
                {
                    continue ;
                }

                size_t sent = send(client_fd, get_rear_send_buf_ptr(&s_ptr->send_bufs), get_rear_send_buf_size(&s_ptr->send_bufs), 0);
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
    close_mariadb(&server_ptr->db);
}