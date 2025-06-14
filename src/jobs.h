/*
 * vush - a simple UNIX shell
 * Licensed under the GNU GPLv3.
 */

#ifndef JOBS_H
#define JOBS_H

#include <sys/types.h>

void add_job(pid_t pid, const char *cmd);
void remove_job(pid_t pid);
int check_jobs(void);
void print_jobs(int mode, int count, int *ids);
pid_t get_job_pid(int id);
int wait_job(int id);
int kill_job(int id, int sig);
int bg_job(int id);
int get_last_job_id(void);
void jobs_sigchld_handler(int sig);

/* PID of the most recently started background job */
extern pid_t last_bg_pid;

#endif /* JOBS_H */
