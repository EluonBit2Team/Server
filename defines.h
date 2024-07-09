#define PORT 3334
#define MAX_CLIENT_NUM 100
#define EPOLL_SIZE MAX_CLIENT_NUM
#define BUFF_SIZE 2048
#define HEADER_SIZE sizeof(int)

#define SHA2_HASH_LENGTH 256

#define SIGN_UP_TYPE 1
#define LOG_IN_TYPE 2
#define MSG_SEND 3
#define GROUP_GEN_REQ_TYPE 4
#define USER_LIST_TYPE 5
#define GROUP_LIST_TYPE 6

#define GENERAL_ERROR 100

#ifndef __USE_GNU
#define __USE_GNU
#endif

#define LOG_FILE "server_status.log"
