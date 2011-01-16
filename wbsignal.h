#ifndef WBOX_SIGNAL_H
#define WBOX_SIGNAL_H

#include <signal.h>

void (*Signal(int signo, void (*func)(int)))(int);

#endif
