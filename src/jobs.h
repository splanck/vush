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

#endif /* JOBS_H */
