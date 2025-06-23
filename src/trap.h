#ifndef TRAP_H
#define TRAP_H
#include <signal.h>
void trap_handler(int sig);
int process_pending_traps(void);
int any_pending_traps(void);
void run_exit_trap(void);
void init_pending_traps(int count);
void free_pending_traps(void);
#endif /* TRAP_H */
