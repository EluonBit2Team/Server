#include "cores/NetCore.h"

int main() {
    epoll_net_core net;
    if (init_server(&net) == false)
    {
        return -1;
    }
    run_server(&net);
    down_server(&net);
}