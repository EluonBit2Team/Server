#include "sig_handle.h"

void handle_signal(int sig) {
    if (sig == SIGINT || sig == SIGSEGV || sig == SIGABRT) {
        printf("Parent received signal %d, terminating child process\n", sig);
        if (g_child_pid > 0) {
            kill(g_child_pid, SIGKILL);
        }
        exit(0);  
    } else if (sig == SIGCHLD) {
        int status;
        pid_t pid = waitpid(g_child_pid, &status, WNOHANG);
        if (pid > 0) {
            printf("Child process %d terminated\n", pid);
            exit(0);
        }
    }
}

void setup_signal_handlers() {
    struct sigaction sa;
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGCHLD, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
}
