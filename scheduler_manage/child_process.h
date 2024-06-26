#ifndef CHILD_PROCESS_H
#define CHILD_PROCESS_H

#include "../includes.h"

#define SERVER_SCHEDULER "scheduler/server_scheduler"

void fork_exec();
void terminate_child();

extern pid_t g_child_pid;

#endif