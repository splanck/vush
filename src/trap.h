#ifndef TRAP_H
#define TRAP_H
#include <signal.h>
void trap_handler(int sig);
int process_pending_traps(void);
int any_pending_traps(void);
void run_exit_trap(void);
#endif /* TRAP_H */
