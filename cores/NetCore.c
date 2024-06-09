#include "NetCore.h"

// (워커스레드들이)할 일의 정보를 담으면, 동기화 기법(뮤텍스)을 고려해서 담는 함수.
void enqueue_task(thread_pool_t* thread_pool, int req_client_fd, int req_service_id, ring_buf *org_buf, int org_data_size)
{
    task new_task;
    new_task.service_id = req_service_id;
    new_task.req_client_fd = req_client_fd;
    ring_array(org_buf,new_task.buf,org_data_size);
    new_task.task_data_len = org_data_size;

    pthread_mutex_lock(&thread_pool->task_mutex);
    enqueue(&thread_pool->task_queue, (void*)&new_task);
    pthread_cond_signal(&thread_pool->task_cond);
    pthread_mutex_unlock(&thread_pool->task_mutex);
}

// 워커스레드에서 할 일을 꺼낼때(des에 복사) 쓰는 함수.
int deqeueu_and_get_task(thread_pool_t* thread_pool, task* des)
{
    pthread_mutex_lock(&thread_pool->task_mutex);
    if (dequeue(&thread_pool->task_queue, (void*)des) < 0)
    {
        pthread_mutex_unlock(&thread_pool->task_mutex);
        return FALSE;
    }
    pthread_mutex_unlock(&thread_pool->task_mutex);
    return TRUE;
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
        if (deqeueu_and_get_task(thread_pool, &temp_task) == TRUE)
        {
            server_ptr->function_array[temp_task.service_id](server_ptr, &temp_task);
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
    // 보낸사람 이외에 전부 출력.
    for (int i = 0; i < MAX_CLIENT_NUM; i++)
    {
        client_session_t* now_session = &server_ptr->session_pool.session_pool[i];
        if (now_session->fd == -1 || task->req_client_fd == now_session->fd)
        {
            continue ;
        }
        reserve_send(&now_session->send_bufs, task->buf, task->task_data_len);

        // send 이벤트 예약
        struct epoll_event temp_event;
        temp_event.events = EPOLLOUT | EPOLLET;
        temp_event.data.fd = now_session->fd;
        if (epoll_ctl(server_ptr->epoll_fd, EPOLL_CTL_MOD, now_session->fd, &temp_event) == -1) {
            perror("epoll_ctl: add");
            close(task->req_client_fd);
        }
    }
}

void set_sock_nonblocking_mode(int sockFd) {
    int flag = fcntl(sockFd, F_GETFL, 0);
    fcntl(sockFd, F_SETFL, flag | O_NONBLOCK);
}

// 서버 초기화
int init_server(epoll_net_core* server_ptr) {
    // 세션 초기화
    init_session(&server_ptr->session_pool, MAX_CLIENT_NUM);

    // 서버 주소 설정
    server_ptr->is_run = FALSE;
    server_ptr->listen_addr.sin_family = AF_INET;
    server_ptr->listen_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_ptr->listen_addr.sin_port = htons(PORT);

    // 서비스 함수 초기화 및 추가
    for (int i = 0; i < SERVICE_FUNC_NUM; i++)
    {
        server_ptr->function_array[i] = NULL;
    }
    server_ptr->function_array[ECHO_SERVICE_FUNC] = echo_service;

    // 리슨소켓 생성
    server_ptr->listen_fd = socket(PF_INET, SOCK_STREAM, 0);
    if (server_ptr->listen_fd < 0)
    {
        printf("listen sock assignment error: %d\n", errno);
    }
    set_sock_nonblocking_mode(server_ptr->listen_fd);
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
    server_ptr->is_run = TRUE;

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
    while (server_ptr->is_run == TRUE) {
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
                printf("accept\n");
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
                
                enqueue_task(&server_ptr->thread_pool, client_fd, ECHO_SERVICE_FUNC, &s_ptr->recv_bufs, input_size);
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
    server_ptr->is_run = FALSE;
    close_all_sessions(&server_ptr->session_pool);
    close(server_ptr->listen_fd);
    close(server_ptr->epoll_fd);
    free(server_ptr->epoll_events);
    for (int i = 0; i < WOKER_THREAD_NUM; i++) {
       pthread_join(server_ptr->thread_pool.worker_threads[i], NULL);
    }
}