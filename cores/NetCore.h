#ifndef NET_CORE_H
#define NET_CORE_H


#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

#include <sys/file.h>
#include <sys/wait.h>
#include <sys/time.h>

#include "../includes.h"
#include "../utilities/ring_buffer.h"
#include "../utilities/void_queue.h"
#include "../utilities/packet_converter.h"
#include "../mariadb/mariadb.h"
#include "../defines.h"
#include "uid_hash_map.h"

#include "session.h"

#define PORT 3334
#define MAX_CLIENT_NUM 100
#define EPOLL_SIZE MAX_CLIENT_NUM

#define SERVICE_FUNC_NUM 30
#define ECHO_SERVICE_FUNC 0
#define SIGNUP_SERV_FUNC 1
#define LOGIN_SERV_FUNC 2
#define MSG_SERV_FUNC 3
#define MAKE_GROUP_SERV_FUNC 4
#define USER_LIST_SERV_FUNC 5
#define GROUP_LIST_SERV_FUNC 6
#define EDIT_MEMBER_SERV_FUNC 7
#define MNG_REQ_LIST_SERV_FUNC 8
#define MNG_SIGNUP_APPROVE_SERV_FUNC 9
#define MNG_GROUP_APPROVE_SERV_FUNC 10
#define GROUP_MEMEMBER_SERV_FUNC 11
//#define CHATTING_SERV_FUNC 12
#define EDIT_MEMBER_INFO_SERV_FUNC 13
#define PRE_CHAT_LOG_SERV_FUNC 14
#define GROUP_DELETE_SERV_FUNC 15
#define SERVER_LOG_SERV_FUNC 16
#define SERVER_STATUS_SERV_FUNC 17
#define DM_SERV_FUNC 18
#define PRE_DM_LOG_SERV_FUNC 19
#define OUT_CHAT_GROUP_SERV_FUNC 20
#define USER_LOG_SERV_FUNC 21
#define CURRENT_USER_LIST_SERV_FUNC 22
#define USER_STATUS_CHANGE_NOTICE 300
#define SERVER_DOWN_NOTICE 301

#define WOKER_THREAD_NUM 8

// 워커스레드가 처리할 일감을 포장한 구조체
struct st_task {
    //int service_id;     // 일감의 종류(에코인지 뭔지...)
    int req_client_fd;  // 일감 요청한 클라이언트 fd
    char buf[BUFF_SIZE];// 처리할 일감
    int task_data_len;  // 처리할 일감이 어느정도 크기인지
} typedef task_t;

// 스레드풀.
struct st_thread_pool {
    // ----- 스레드간 일감(테스크)을 동기화 처리할 큐(비슷한 무언가) -----
    pthread_mutex_t task_mutex; // 락
    pthread_cond_t task_cond;   // 대기중인 스레드를 깨워줄 컨디션벨류
    void_queue_t task_queue;
    pthread_t worker_threads[WOKER_THREAD_NUM]; // 워커스레드들
} typedef thread_pool_t;

struct st_epoll_net_core;   // 전방선언
typedef void (*func_ptr)(struct st_epoll_net_core*, task_t*); // 서비스함수포인터 타입 지정.
typedef struct st_epoll_net_core {
    bool is_run;    // 서버 내릴때 flase(지금은)
    int listen_fd;  // 서버 리슨용 소켓 fd

    func_ptr function_array[SERVICE_FUNC_NUM]; // 서비스 배열
    session_pool_t session_pool;
    struct sockaddr_in listen_addr; // 리슨용 소켓 주소 담는 자료형
    int_hash_map_t fd_to_uid_hash;
    int_hash_map_t uid_to_fd_hash;
    
    int epoll_fd; 
    struct epoll_event* epoll_events;

    thread_pool_t thread_pool; // 서버에서 사용할 워커스레드

    chatdb_t db; 
} epoll_net_core;

// 서버 세팅 함수들 -> main에서 호출하여 조작.
bool init_server(epoll_net_core* server_ptr) ;
int run_server(epoll_net_core* server_ptr) ;
void down_server(epoll_net_core* server_ptr);
void fix_log_time_pairs(epoll_net_core* server_ptr);

// 스레드 풀 관련 초기화
void init_worker_thread(epoll_net_core* server_ptr, thread_pool_t* thread_pool_t_ptr);
// 워커스레드가 무한반 복할 루틴.
void* work_routine(void *ptr);
// (워커스레드들이)할 일의 정보를 담으면, 동기화 기법(뮤텍스)을 고려해서 담는 함수.
bool enqueue_task(thread_pool_t* thread_pool, int req_client_fd, ring_buf* org_buf, int org_data_size);
// 워커스레드에서 할 일을 꺼낼때(des에 복사) 쓰는 함수.
bool deqeueu_and_get_task(thread_pool_t* thread_pool, task_t* des);
// accept시 동작 처리 함수
int accept_client(epoll_net_core* server_ptr); 
void disconnect_client(epoll_net_core* server_ptr, int client_fd);
void set_sock_nonblocking_mode(int sockFd) ;

char* get_front_send_buf_ptr(void_queue_t* vq);
size_t get_front_send_buf_size(void_queue_t* vq);
void reserve_send(void_queue_t* vq, char* send_org, int send_size);
void reserve_epoll_send(int epoll_fd, client_session_t* send_session, char* send_org, int send_size);

// ✨ 서비스 함수. 이런 형태의 함수들을 추가하여 서비스 추가. ✨
void echo_service(epoll_net_core* server_ptr, task_t* task);
void login_service(epoll_net_core* server_ptr, task_t* task);
void signup_service(epoll_net_core* server_ptr, task_t* task);
void make_group_service(epoll_net_core* server_ptr, task_t* task);
void user_list_service(epoll_net_core* server_ptr, task_t* task);
void group_list_service(epoll_net_core* server_ptr, task_t* task);
void edit_group_member_service(epoll_net_core* server_ptr, task_t* task);
void mng_req_list_service(epoll_net_core* server_ptr, task_t* task);
void mng_signup_approve_service(epoll_net_core* server_ptr, task_t* task);
void mng_group_approve_service(epoll_net_core* server_ptr, task_t* task);
void mng_user_log_service(epoll_net_core* server_ptr, task_t* task);
void mng_server_log_service(epoll_net_core* server_ptr, task_t* task);
void mng_server_status_service(epoll_net_core* server_ptr, task_t* task);
void mng_edit_user_info_service(epoll_net_core* server_ptr, task_t* task);
void get_group_members_service(epoll_net_core* server_ptr, task_t* task);
void group_delete_service(epoll_net_core* server_ptr, task_t* task);
void chat_in_group_service(epoll_net_core* server_ptr, task_t* task);
void pre_chat_log_service(epoll_net_core* server_ptr, task_t* task);
void chat_in_user_service(epoll_net_core* server_ptr, task_t* task);
void pre_dm_log_service(epoll_net_core* server_ptr, task_t* task);
void out_chat_group_service(epoll_net_core* server_ptr, task_t* task);
void user_log_service(epoll_net_core* server_ptr, task_t* task);
void current_user_list_service(epoll_net_core* server_ptr, task_t* task);
// notice. from 서버 to 클라이언트. 응답 안기다림.
void user_status_change_notice(epoll_net_core* server_ptr, conn_t* user_setting_conn);
void server_down_notice_to_all(epoll_net_core* server_ptr);

#endif