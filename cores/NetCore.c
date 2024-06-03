#include "NetCore.h"

// (워커스레드들이)할 일의 정보를 담으면, 동기화 기법(뮤텍스)을 고려해서 담는 함수.
void enqueue_task(thread_pool_t* thread_pool, int req_client_fd, int req_service_id, char* org_buf, int org_data_size)
{
    pthread_mutex_lock(&thread_pool->task_mutex);
    // 이미 쌓여 있는 할 일의 개수가 너무 많으면 무시함
    if (thread_pool->task_cnt == MAX_TASK_SIZE) // 💥TODO 개수 제한 풀기
    {
        pthread_mutex_unlock(&thread_pool->task_mutex);
        return ;
    }

    // 할 일 추가
    task* queuing_task = &thread_pool->tasks[thread_pool->task_cnt++]; // 💥TODO 개수 제한 풀기
    //printf("%d task enqueue\n", thread_pool->task_cnt);
    queuing_task->service_id = req_service_id;
    queuing_task->req_client_fd = req_client_fd;
    memcpy(queuing_task->buf, org_buf, org_data_size);
    queuing_task->task_data_len = org_data_size;

    // 할 일이 생겼으니 대기중인 스레드는 일어나라는 신호(컨디션벨류)
    pthread_cond_signal(&thread_pool->task_cond);
    pthread_mutex_unlock(&thread_pool->task_mutex);
}

// 워커스레드에서 할 일을 꺼낼때(des에 복사) 쓰는 함수.
int deqeueu_and_get_task(thread_pool_t* thread_pool, task* des)
{
    pthread_mutex_lock(&thread_pool->task_mutex);
    // 꺼낼게 없으면 반환
    if (thread_pool->task_cnt == 0) // 💥TODO 개수 제한 풀기
    {
        pthread_mutex_unlock(&thread_pool->task_mutex);
        return FALSE;
    }

    // 할 일 복사
    task* dequeuing_task = &thread_pool->tasks[--thread_pool->task_cnt]; // 💥TODO 개수 제한 풀기
    des->req_client_fd = dequeuing_task->req_client_fd;
    des->service_id = dequeuing_task->service_id;
    memcpy(des->buf, dequeuing_task->buf, dequeuing_task->task_data_len);
    des->task_data_len = dequeuing_task->task_data_len;

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
        while (thread_pool->task_cnt == 0) {
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
    for (int i = 0; i < WOKER_THREAD_NUM; i++)
    {
        pthread_create(&thread_pool_t_ptr->worker_threads[i], NULL, work_routine, server_ptr);
    }
}

// ✨ 서비스 함수. 이런 형태의 함수들을 추가하여 서비스 추가. ✨
void echo_service(epoll_net_core* server_ptr, task* task) {
    // 보낸사람 이외에 전부 출력.
    for (int i = 0; i < MAX_CLIENT_NUM; i++)
    {
        if (server_ptr->client_sessions[i].fd == -1
            || task->req_client_fd == server_ptr->client_sessions[i].fd)
        {
            continue ;
        }

        // 지금 바로 send할 수 있는지 알 수 없으니 send 이벤트 예약
        struct epoll_event temp_event;
        temp_event.events = EPOLLOUT | EPOLLET;
        temp_event.data.fd = server_ptr->client_sessions[i].fd;
        if (epoll_ctl(server_ptr->epoll_fd, EPOLL_CTL_MOD, server_ptr->client_sessions[i].fd, &temp_event) == -1) {
            perror("epoll_ctl: add");
            close(task->req_client_fd);
        }
        // 지금 바로 send할 수 없으므로, 나중에 확인 가능한 세션 버퍼에 데이터를 저장
        memcpy(server_ptr->client_sessions[i].send_buf, task->buf, task->task_data_len);
        server_ptr->client_sessions[i].send_data_size = task->task_data_len;
    }
}

void set_sock_nonblocking_mode(int sockFd) {
    int flag = fcntl(sockFd, F_GETFL, 0);
    fcntl(sockFd, F_SETFL, flag | O_NONBLOCK);
}

// 서버 초기화
int init_server(epoll_net_core* server_ptr) {
    // 세션 초기화
    for (int i = 0; i < MAX_CLIENT_NUM; i++)
    {
        server_ptr->client_sessions[i].fd = -1;
        server_ptr->client_sessions[i].send_data_size = -1;
    }

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
        printf("listen sock assignment error: \n", errno);
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
        printf("accept error: \n", errno);
    }

    set_sock_nonblocking_mode(client_sock);

    // 세션 초기화
    server_ptr->client_sessions[client_sock].fd = client_sock;
    ring_buffer_init(&server_ptr->client_sessions[client_sock].recv_buf);
    //memset(server_ptr->client_sessions[client_sock].send_buf, 0, BUFF_SIZE);

    temp_event.data.fd = client_sock;
    // ✨ 엣지트리거방식의(EPOLLIN) 입력 이벤트 대기 설정(EPOLLET)
    temp_event.events = EPOLLIN | EPOLLET;
    epoll_ctl(server_ptr->epoll_fd, EPOLL_CTL_ADD, client_sock, &temp_event);
    printf("accept \n client", client_sock);
}

void disconnect_client(epoll_net_core* server_ptr, int client_fd)
{
    epoll_ctl(server_ptr->epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
    // TODO: 세션 데이터 추가 필요
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
                char buffer[BUFF_SIZE];
                int input_size = read(client_fd, buffer, BUFF_SIZE);
                if (input_size == 0)
                {
                    printf("input_size == 0\n");
                    disconnect_client(server_ptr, client_fd);
                }
                else if (input_size < 0)
                {
                    // errno EAGAIN?
                    printf("input_size < 0\n");
                }
                else
                {
                    ring_buffer_write(&server_ptr->client_sessions[client_fd].recv_buf, buffer, input_size); // 링 버퍼에 쓰기
                    sleep(3);
                    // 워커 스레드에게 일감을 넣어줌
                    enqueue_task(
                        &server_ptr->thread_pool, client_fd, ECHO_SERVICE_FUNC, 
                        server_ptr->client_sessions[client_fd].recv_buf.buffer, input_size);
                }
            }
            // 이벤트에 입력된 fd의 send버퍼가 비어서, send가능할시 발생하는 이벤트
            else if (server_ptr->epoll_events[i].events & EPOLLOUT) {
                int client_fd = server_ptr->epoll_events[i].data.fd;
                // send버퍼가 비어있으므로, send가 무조건 성공한다는게 보장되므 send수행
                //  -> send에 실패하여 EWOULDBLOCK가 에러가 뜨는 상황을 피하는 것.
                ssize_t sent = send(
                    client_fd, 
                    server_ptr->client_sessions[client_fd].send_buf, 
                    server_ptr->client_sessions[client_fd].send_data_size, 0);
                if (sent < 0) {
                    perror("send");
                    close(server_ptr->epoll_events[i].data.fd);
                }
                
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

void down_server(epoll_net_core* server_ptr)
{
    server_ptr->is_run = FALSE;
    close(server_ptr->listen_fd);
    close(server_ptr->epoll_fd);
    free(server_ptr->epoll_events);
}