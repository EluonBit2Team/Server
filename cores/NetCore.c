#include "NetCore.h"

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
    //printf("enqueue_task : %d\n", thread_pool->task_queue.type_default_size);
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
        while (is_empty(&thread_pool->task_queue) == true && server_ptr->is_run == true) {
            pthread_cond_wait(&thread_pool->task_cond, &thread_pool->task_mutex);
        }
        pthread_mutex_unlock(&thread_pool->task_mutex);
        if (server_ptr->is_run == false)
        {
            break;
        }
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
            //printf("type num:%d\n", type);
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

char* get_front_send_buf_ptr(void_queue_t* vq)
{
    if (get_front_node(vq) == NULL)
    {
        return NULL;
    }

    send_buf_t* front_node = (send_buf_t*)get_front_node(vq);
    if (front_node == NULL) {
        return NULL;
    }

    return ((send_buf_t*)get_front_node(vq))->buf_ptr;
}

// todo : queue함수로 옮기기.
size_t get_front_send_buf_size(void_queue_t* vq)
{
    //return *((size_t*)get_rear_data(vq));
    return ((send_buf_t*)get_front_node(vq))->send_data_size;
}

void reserve_epoll_send(int epoll_fd, client_session_t* send_session, char* send_org, int send_size) {
    if (send_session == NULL) {
        printf("reserve_epoll_send get NULL send_session\n");
        return ;
    }
    struct epoll_event temp_send_event;
    temp_send_event.events = EPOLLOUT | EPOLLET;
    temp_send_event.data.fd = send_session->fd;
    reserve_send(&send_session->send_bufs, send_org, send_size);
    if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, send_session->fd, &temp_send_event) == -1) {
        perror("epoll_ctl: add");
    }
}

// 세션, epoll, 보낼 데이터 원본 및 크기 
// todo : 좀 더 일반적인 형태로. queueu를 받지 않고, serv랑 세션을 받게.
void reserve_send(void_queue_t* vq, char* send_org, int body_size)
{
    int total_size = HEADER_SIZE + body_size;
    send_buf_t temp_send_buf;
    // send_size는 int여야함.
    // malloc(): corrupted top size -> enqueue내부 malloc에서 발생.
    // 하지만 실제 문제는 아래 malloc에서 할당한 사이즈를 넘어서 데이터를 조작해서 발생
    //  -> sizeof(char) * body_size를 할당받았지만 실제로 조작한 데이터 크기는 HEADER_SIZE + sizeof(char) * body_size여서 발생.
    temp_send_buf.buf_ptr = (char*)malloc(HEADER_SIZE + sizeof(char) * body_size);
    temp_send_buf.send_data_size = total_size;
    memcpy(temp_send_buf.buf_ptr, (char*)&total_size, HEADER_SIZE);
    memcpy(temp_send_buf.buf_ptr + HEADER_SIZE, send_org, body_size);
    enqueue(vq, (void*)&temp_send_buf);
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
    server_ptr->function_array[MSG_SERV_FUNC] = chat_in_group_service;
    server_ptr->function_array[MAKE_GROUP_SERV_FUNC] = make_group_service;
    server_ptr->function_array[USER_LIST_SERV_FUNC] = user_list_service;
    server_ptr->function_array[GROUP_LIST_SERV_FUNC] = group_list_service;
    server_ptr->function_array[EDIT_MEMBER_SERV_FUNC] = edit_member_service;
    server_ptr->function_array[MNG_REQ_LIST_SERV_FUNC] = Mng_req_list_service;
    server_ptr->function_array[MNG_SIGNUP_APPROVE_SERV_FUNC] = Mng_signup_approve_service;
    server_ptr->function_array[MNG_GROUP_APPROVE_SERV_FUNC] = Mng_group_approve_service;
    server_ptr->function_array[GROUP_MEMEMBER_SERV_FUNC] = group_member_service;
    server_ptr->function_array[CHATTING_SERV_FUNC] = chat_in_group_service;
    server_ptr->function_array[EDIT_MEMBER_INFO_SERV_FUNC] = edit_user_info_service;
    server_ptr->function_array[PRE_CHAT_LOG_SERV_FUNC] = pre_chat_log_service;
    server_ptr->function_array[GROUP_DELETE_SERV_FUNC] = group_delete_service;
    server_ptr->function_array[SERVER_LOG_SERV_FUNC] = server_log_service;
    server_ptr->function_array[SERVER_STATUS_SERV_FUNC] = server_status_service;

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
        fprintf(stderr, "logout query fail: %s\n", mysql_error(conn->conn));
    }
    epoll_ctl(server_ptr->epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
    int uid = find(&server_ptr->fd_to_uid_hash, client_fd);
    erase(&server_ptr->fd_to_uid_hash, client_fd);
    erase(&server_ptr->uid_to_fd_hash, uid);
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

void set_serverlog(epoll_net_core* server_ptr) {
    conn_t* conn = NULL;
    char SQL_buf[512];
    conn = get_conn(&server_ptr->db.pools[LOG_DB_IDX]);

    snprintf(SQL_buf, sizeof(SQL_buf), "UPDATE client_log SET logout_time = NOW() WHERE logout_time IS NULL");
    if (mysql_query(conn->conn, SQL_buf)) {
        fprintf(stderr, "UPDATE client_log failed: %s\n", mysql_error(conn->conn));
    }

    snprintf(SQL_buf, sizeof(SQL_buf), "UPDATE server_log SET downtime = NOW() WHERE downtime IS NULL");
    if (mysql_query(conn->conn, SQL_buf)) {
        fprintf(stderr, "UPDATE server_log server_status failed: %s\n", mysql_error(conn->conn));
    }

    snprintf(SQL_buf, sizeof(SQL_buf), "INSERT INTO server_log (uptime) VALUES (NOW())");
    if (mysql_query(conn->conn, SQL_buf)) {
        fprintf(stderr, "UPDATE server_log timestamp failed: %s\n", mysql_error(conn->conn));
    }

    if (conn != NULL) {
        release_conn(&server_ptr->db.pools[LOG_DB_IDX], conn);
    }
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

    temp_epoll_event.events = EPOLLIN;
    temp_epoll_event.data.fd = STDIN_FILENO;
    rt_val = epoll_ctl(server_ptr->epoll_fd, EPOLL_CTL_ADD, temp_epoll_event.data.fd, &temp_epoll_event);
    if (rt_val < 0) {
        printf("epoll_ctl Error : %d\n", errno);
    }

    set_serverlog(server_ptr);

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
            if (server_ptr->epoll_events[i].data.fd == STDIN_FILENO) {
                return 0;
            }
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

                    conn_t* user_status_conn = get_conn(&server_ptr->db.pools[USER_SETTING_DB_IDX]);
                    user_status_change_notice(server_ptr, user_status_conn);
                    release_conns(&server_ptr->db, 1, user_status_conn);
                    
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
                while (1) {
                    char* send_buf_ptr = get_front_send_buf_ptr(&s_ptr->send_bufs);
                    printf("send_buf_ptr : %p, get_front_send_buf_size: %ld\n", send_buf_ptr, get_front_send_buf_size(&s_ptr->send_bufs));
                    if (send_buf_ptr == NULL)
                    {
                        break ;
                    }
                    size_t sent = send(client_fd, send_buf_ptr, get_front_send_buf_size(&s_ptr->send_bufs), 0);
                    // 필요할때 주석 풀기.
                    write(STDOUT_FILENO, "SEND:", 5); write(STDOUT_FILENO, send_buf_ptr, get_front_send_buf_size(&s_ptr->send_bufs)); write(STDOUT_FILENO, "\n", 1);
                    if (sent < 0) {
                        perror("send");
                        close(server_ptr->epoll_events[i].data.fd);
                    }
                    send_buf_t temp;
                    dequeue(&s_ptr->send_bufs, &temp);
                    free_all(1, temp.buf_ptr);
                    // send할 때 이벤트를 변경(EPOLL_CTL_MOD)해서 보내는 이벤트로 바꿨으니
                    // 다시 통신을 받는 이벤트로 변경하여 유저의 입력을 대기.
                }
                struct epoll_event temp_event;
                temp_event.events = EPOLLIN | EPOLLET;
                //temp_event.data.fd = server_ptr->epoll_events[i].data.fd;
                temp_event.data.fd = client_fd;
                if (epoll_ctl(server_ptr->epoll_fd, EPOLL_CTL_MOD, client_fd, &temp_event) == -1) {
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
    conn_t* conn = NULL;
    char SQL_buf[512];
    conn = get_conn(&server_ptr->db.pools[LOG_DB_IDX]);

    snprintf(SQL_buf, sizeof(SQL_buf), "UPDATE server_log SET downtime = NOW() WHERE downtime IS NULL;");

    if (mysql_query(conn->conn, SQL_buf)) {
        fprintf(stderr, "UPDATE server_log timestamp failed: %s\n", mysql_error(conn->conn));
    }
    if (conn != NULL) {
        release_conn(&server_ptr->db.pools[LOG_DB_IDX], conn);
    }
    server_ptr->is_run = false;
    epoll_ctl(server_ptr->epoll_fd, EPOLL_CTL_DEL, server_ptr->listen_fd, NULL);
    close_all_sessions(server_ptr->epoll_fd, &server_ptr->session_pool);
    close(server_ptr->listen_fd);
    close(server_ptr->epoll_fd);
    free(server_ptr->epoll_events);
    for (int i = 0; i < WOKER_THREAD_NUM; i++) {
        pthread_cond_signal(&server_ptr->thread_pool.task_cond);
    }
    for (int i = 0; i < WOKER_THREAD_NUM; i++) {
        pthread_join(server_ptr->thread_pool.worker_threads[i], NULL);
    }
    for (int i = 0; i < WOKER_THREAD_NUM; i++) {
        pthread_mutex_destroy(&server_ptr->thread_pool.task_mutex);
        pthread_cond_destroy(&server_ptr->thread_pool.task_cond);
    }
    clear_hash_map(&server_ptr->fd_to_uid_hash);
    clear_hash_map(&server_ptr->uid_to_fd_hash);
    close_mariadb(&server_ptr->db);
    printf("server down\n");
}