#include "child_process.h"

pid_t g_child_pid;

void fork_exec() {
    g_child_pid = fork();
    if (g_child_pid < 0) {
        perror("fork failed");
        exit(1);
    }
    else if (g_child_pid == 0) {
        execl(SERVER_SCHEDULER, "Server_Scheduler", (char *)NULL);
        perror("execl failed");
        exit(1);
    }
    else {
        printf("Server PID: %d, Scheduler PID: %d\n", getpid(), g_child_pid);
    }
    
}

void terminate_child() {
    kill(g_child_pid, SIGTERM);
}