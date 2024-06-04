#include "cores/NetCore.h"

int main()
{
    printf("1\n");
    epoll_net_core net;
    printf("2\n");
    init_server(&net);
    printf("3\n");
    run_server(&net);
    printf("4\n");
    down_server(&net);
}