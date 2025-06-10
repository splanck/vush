/*
 * vush - a simple UNIX shell
 * Licensed under the GNU GPLv3.
 */

#ifndef JOBS_H
#define JOBS_H

#include <sys/types.h>

void add_job(pid_t pid, const char *cmd);
void remove_job(pid_t pid);
void check_jobs(void);
void print_jobs(void);
pid_t get_job_pid(int id);
int wait_job(int id);
int kill_job(int id, int sig);
int bg_job(int id);

#endif /* JOBS_H */
