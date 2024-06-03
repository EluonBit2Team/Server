#ifndef NET_CORE_H

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
    int task_cnt;               // 큐 비스므리한 방식으로 쓰기 위한 카운터
    task tasks[MAX_TASK_SIZE];  // 일감
    // -------------------------------------------------------
    
    pthread_t worker_threads[WOKER_THREAD_NUM]; // 워커스레드들
} typedef thread_pool_t;

struct st_client_session {
    int fd;                     // 세션 fd
    // 💥TODO 일반적인 형태로.
    ring_buffer_t recv_buf;   // 유저별 소켓으로 받은 데이터를 저장할 버퍼(일감 가공 전 날것의 데이터)
    char send_buf[BUFF_SIZE];
    int send_data_size;         // 유저로부터 
} typedef client_session;

struct st_epoll_net_core;   // 전방선언
typedef void (*func_ptr)(struct st_epoll_net_core*, task*); // 서비스함수포인터 타입 지정.
typedef struct st_epoll_net_core {
    int is_run;     // 서버 내릴때 flase(지금은)
    int listen_fd;  // 서버 리슨용 소켓 fd

    func_ptr function_array[SERVICE_FUNC_NUM]; // 서비스 배열
    
     // 💥TODO 개수 제한 풀기
    client_session client_sessions[MAX_CLIENT_NUM]; // 연결된 클라이언트들 관리할 세션 배열
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
void enqueue_task(thread_pool_t* thread_pool, int req_client_fd, int req_service_id, char* org_buf, int org_data_size);
// 워커스레드에서 할 일을 꺼낼때(des에 복사) 쓰는 함수.
int deqeueu_and_get_task(thread_pool_t* thread_pool, task* des);

// accept시 동작 처리 함수
int accept_client(epoll_net_core* server_ptr); 
void disconnect_client(epoll_net_core* server_ptr, int client_fd);
void set_sock_nonblocking_mode(int sockFd) ;

// ✨ 서비스 함수. 이런 형태의 함수들을 추가하여 서비스 추가. ✨
void echo_service(epoll_net_core* server_ptr, task* task) ;

#endif