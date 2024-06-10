#ifndef NET_CORE_H
#define NET_CORE_H

#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

#include "../utilities/ring_buffer.h"
#include "../utilities/void_queue.h"
#include "../utilities/packet_converter.h"
#include "session.h"

#define TRUE 1
#define FALSE 0
#define PORT 3334
#define MAX_CLIENT_NUM 100
#define EPOLL_SIZE MAX_CLIENT_NUM
#define BUFF_SIZE 1024

#define SERVICE_FUNC_NUM 10
#define ECHO_SERVICE_FUNC 0

#define WOKER_THREAD_NUM 4
#define MAX_TASK_SIZE 100

// 워커스레드가 처리할 일감을 포장한 구조체
struct st_task {
    int service_id;     // 일감의 종류(에코인지 뭔지...)
    int req_client_fd;  // 일감 요청한 클라이언트 fd
    char buf[BUFF_SIZE];// 처리할 일감
    int task_data_len;  // 처리할 일감이 어느정도 크기인지
} typedef task;

// 스레드풀.
struct st_thread_pool {

    // ----- 스레드간 일감(테스크)을 동기화 처리할 큐(비슷한 무언가) -----
    pthread_mutex_t task_mutex; // 락
    pthread_cond_t task_cond;   // 대기중인 스레드를 깨워줄 컨디션벨류
    void_queue_t task_queue;
    pthread_t worker_threads[WOKER_THREAD_NUM]; // 워커스레드들
} typedef thread_pool_t;

typedef struct send_buf {
    size_t send_data_size;
    char buf[BUFF_SIZE];
} send_buf_t;

struct st_epoll_net_core;   // 전방선언
typedef void (*func_ptr)(struct st_epoll_net_core*, task*); // 서비스함수포인터 타입 지정.
typedef struct st_epoll_net_core {
    int is_run;     // 서버 내릴때 flase(지금은)
    int listen_fd;  // 서버 리슨용 소켓 fd

    func_ptr function_array[SERVICE_FUNC_NUM]; // 서비스 배열
    session_pool_t session_pool;
    struct sockaddr_in listen_addr; // 리슨용 소켓 주소 담는 자료형
    
    int epoll_fd; 
    struct epoll_event* epoll_events;

    thread_pool_t thread_pool; // 서버에서 사용할 워커스레드
} epoll_net_core;

// 서버 세팅 함수들 -> main에서 호출하여 조작.
int init_server(epoll_net_core* server_ptr) ;
int run_server(epoll_net_core* server_ptr) ;
void down_server(epoll_net_core* server_ptr);

// 스레드 풀 관련 초기화
void init_worker_thread(epoll_net_core* server_ptr, thread_pool_t* thread_pool_t_ptr);
// 워커스레드가 무한반 복할 루틴.
void* work_routine(void *ptr);
// (워커스레드들이)할 일의 정보를 담으면, 동기화 기법(뮤텍스)을 고려해서 담는 함수.
void enqueue_task(thread_pool_t* thread_pool, int req_client_fd, int req_service_id, ring_buf* org_buf, int org_data_size);
// 워커스레드에서 할 일을 꺼낼때(des에 복사) 쓰는 함수.
int deqeueu_and_get_task(thread_pool_t* thread_pool, task* des);
// accept시 동작 처리 함수
int accept_client(epoll_net_core* server_ptr); 
void disconnect_client(epoll_net_core* server_ptr, int client_fd);
void set_sock_nonblocking_mode(int sockFd) ;

char* get_rear_send_buf_ptr(void_queue_t* vq);
size_t get_rear_send_buf_size(void_queue_t* vq);
void reserve_send(void_queue_t* vq, char* send_org, size_t send_size);
// ✨ 서비스 함수. 이런 형태의 함수들을 추가하여 서비스 추가. ✨
void echo_service(epoll_net_core* server_ptr, task* task) ;

#endif