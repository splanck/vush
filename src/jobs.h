/*
 * vush - a simple UNIX shell
 * Licensed under the BSD 2-Clause Simplified License.
 */

#ifndef JOBS_H
#define JOBS_H

#include <sys/types.h>
#include <signal.h>

void add_job(pid_t pid, const char *cmd);
void remove_job(pid_t pid);
int check_jobs(void);
int check_jobs_internal(int prefix);
void print_jobs(int mode, int filter, int changed_only, int count, int *ids);
pid_t get_job_pid(int id);
int wait_job(int id);
int kill_job(int id, int sig);
int bg_job(int id);
int get_last_job_id(void);
int parse_job_spec(const char *spec);
void jobs_sigchld_handler(int sig);

/* True while the shell is waiting for input at the prompt */
extern volatile sig_atomic_t jobs_at_prompt;
extern volatile sig_atomic_t jobs_changed;

/* PID of the most recently started background job */
extern pid_t last_bg_pid;

#endif /* JOBS_H */
