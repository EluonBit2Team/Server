#include "cores/NetCore.h"
#include "scheduler_manage/child_process.h"
#include "scheduler_manage/sig_handle.h"

int main() {
    epoll_net_core net;
    setup_signal_handlers();
    fork_exec();

    if (init_server(&net) == false) {
        return -1;
    }

    run_server(&net);

    down_server(&net);
    terminate_child();
}