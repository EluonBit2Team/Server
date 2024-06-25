#ifndef SIG_HANDLE_H
#define SIG_HANDLE_H
#include "child_process.h"

void setup_signal_handlers();
void handle_signal(int sig);

#endif