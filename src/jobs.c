/*
 * vush - a simple UNIX shell
 * Licensed under the GNU GPLv3.
 *
 * Job control helpers
 * -------------------
 *
 * Background processes started by the shell are stored in a simple
 * singly linked list.  Each list entry keeps the job's process ID,
 * a unique job number and the command line that was executed.  This
 * allows builtins such as `jobs`, `fg`, `bg` and `kill` to keep track
 * of active tasks and to notify the user when a background job
 * completes.
 */
#define _GNU_SOURCE
#include "jobs.h"
#include "parser.h"  /* for MAX_LINE */
#include "options.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <ctype.h>
#include <time.h>

typedef enum { JOB_RUNNING, JOB_STOPPED } JobState;

typedef struct Job {
    int id;
    pid_t pid;
    JobState state;
    char cmd[MAX_LINE];
    struct Job *next;
} Job;

static Job *jobs = NULL;
static int next_job_id = 1;
/* PID of the most recently started background job */
pid_t last_bg_pid = 0;
/* ID of the most recently started background job */
static int last_bg_id = 0;
/* Flag used by the SIGCHLD handler to know when we are at the prompt */
volatile sig_atomic_t jobs_at_prompt = 0;

/* Forward declaration for signal handler */
void jobs_sigchld_handler(int sig);

/*
 * Record a child process that was started in the background.
 * Called by the executor whenever a command is launched with '&'.
 */
void add_job(pid_t pid, const char *cmd) {
    last_bg_pid = pid;
    int id = next_job_id++;
    last_bg_id = id;
    if (!opt_monitor)
        return;
    Job *job = malloc(sizeof(Job));
    if (!job)
        return;
    job->id = id;
    job->pid = pid;
    job->state = JOB_RUNNING;
    strncpy(job->cmd, cmd, MAX_LINE - 1);
    job->cmd[MAX_LINE - 1] = '\0';
    job->next = jobs;
    jobs = job;
}

/*
 * Delete a job entry once the process has terminated or has been
 * brought to the foreground.  The PID must match the job to remove.
 */
void remove_job(pid_t pid) {
    Job **curr = &jobs;
    while (*curr) {
        if ((*curr)->pid == pid) {
            Job *tmp = *curr;
            *curr = (*curr)->next;
            free(tmp);
            return;
        }
        curr = &((*curr)->next);
    }
}
/*
 * Reap finished background processes and print a message when they
 * exit.  This function is typically invoked before displaying a new
 * prompt so that completed jobs are noticed.
 */
/* prefix: 0=no prefix, 1=prepend newline, 2=carriage return */
int check_jobs_internal(int prefix) {
    int printed = 0;
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
        Job *curr = jobs;
        while (curr && curr->pid != pid)
            curr = curr->next;

        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            if (curr && opt_monitor && opt_notify) {
                if (prefix && !printed) {
                    if (prefix == 1)
                        printf("\n");
                    else if (prefix == 2)
                        printf("\r");
                }
                const char *cmd = curr ? curr->cmd : "?";
                char tmp[MAX_LINE];
                strncpy(tmp, cmd, sizeof(tmp) - 1);
                tmp[sizeof(tmp) - 1] = '\0';
                size_t len = strlen(tmp);
                while (len > 0 && isspace((unsigned char)tmp[len - 1]))
                    tmp[--len] = '\0';
                if (len > 0 && tmp[len - 1] == '&') {
                    tmp[--len] = '\0';
                    while (len > 0 && isspace((unsigned char)tmp[len - 1]))
                        tmp[--len] = '\0';
                }
                printf("[vush] job %d (%s &) finished\n",
                       curr ? curr->id : pid,
                       tmp);
                printed = 1;
            }
            remove_job(pid);
        } else if (WIFSTOPPED(status)) {
            if (curr)
                curr->state = JOB_STOPPED;
        } else if (WIFCONTINUED(status)) {
            if (curr)
                curr->state = JOB_RUNNING;
        }
    }
    return printed;
}

int check_jobs(void) {
    int prefix = jobs_at_prompt ? 2 : 1;
    return check_jobs_internal(prefix);
}

/* SIGCHLD handler to reap finished background jobs even when
 * waiting for input. Simply delegate to check_jobs(). */
void jobs_sigchld_handler(int sig) {
    (void)sig;
    int prefix = jobs_at_prompt ? 1 : 0;
    if (check_jobs_internal(prefix) && jobs_at_prompt) {
        const char *ps = getenv("PS1");
        printf("%s", ps ? ps : "vush> ");
        fflush(stdout);
        /* Prevent the prompt from being printed twice */
        jobs_at_prompt = 0;
    }
}

/*
 * Display the list of active background jobs.  Used by the `jobs`
 * builtin to show the user what is currently running.
 */
static const char *job_state_str(JobState s) {
    return s == JOB_STOPPED ? "Stopped" : "Running";
}

static Job *find_job(int id) {
    Job *curr = jobs;
    while (curr) {
        if (curr->id == id)
            return curr;
        curr = curr->next;
    }
    return NULL;
}

static void print_job(Job *j, int mode) {
    if (mode == 2) {
        printf("%d\n", j->pid);
    } else if (mode == 1) {
        printf("[%d] %d %s %s\n", j->id, j->pid, job_state_str(j->state), j->cmd);
    } else {
        printf("[%d] %d %s\n", j->id, j->pid, j->cmd);
    }
}

void print_jobs(int mode, int count, int *ids) {
    if (count > 0) {
        for (int i = 0; i < count; i++) {
            Job *j = find_job(ids[i]);
            if (j)
                print_job(j, mode);
            else
                fprintf(stderr, "jobs: %d: no such job\n", ids[i]);
        }
        return;
    }
    Job *j = jobs;
    while (j) {
        print_job(j, mode);
        j = j->next;
    }
}

/* Return the PID for a job ID or -1 when not found */
pid_t get_job_pid(int id) {
    Job *curr = jobs;
    while (curr) {
        if (curr->id == id)
            return curr->pid;
        curr = curr->next;
    }
    return -1;
}

/*
 * Bring the specified job to the foreground and wait for it to finish.
 * Implements the `fg` builtin.
 */
int wait_job(int id) {
    Job **curr = &jobs;
    while (*curr) {
        if ((*curr)->id == id) {
            int status;
            waitpid((*curr)->pid, &status, 0);
            Job *tmp = *curr;
            *curr = (*curr)->next;
            free(tmp);
            return 0;
        }
        curr = &((*curr)->next);
    }
    fprintf(stderr, "fg: job %d not found\n", id);
    return -1;
}

/*
 * Send the given signal to a job.  Used by the `kill` builtin to
 * terminate or control background processes.
 */
int kill_job(int id, int sig) {
    Job *curr = jobs;
    while (curr) {
        if (curr->id == id) {
            if (kill(curr->pid, sig) != 0) {
                perror("kill");
                return -1;
            }
            if (sig == SIGSTOP)
                curr->state = JOB_STOPPED;
            else if (sig == SIGCONT)
                curr->state = JOB_RUNNING;
            return 0;
        }
        curr = curr->next;
    }
    fprintf(stderr, "kill: job %d not found\n", id);
    return -1;
}

/*
 * Resume a stopped job and keep it running in the background.  This
 * mirrors the behaviour of the `bg` builtin found in other shells.
 */
int bg_job(int id) {
    Job *curr = jobs;
    while (curr) {
        if (curr->id == id) {
            sigset_t mask, old;
            sigemptyset(&mask);
            sigaddset(&mask, SIGCHLD);
            sigprocmask(SIG_BLOCK, &mask, &old);

            if (kill(curr->pid, SIGCONT) != 0) {
                perror("bg");
                sigprocmask(SIG_SETMASK, &old, NULL);
                return -1;
            }
            curr->state = JOB_RUNNING;

            /*
             * If the process exits immediately after being continued we
             * want to print the completion message before the next prompt
             * appears.  Block SIGCHLD so the handler does not print the
             * message itself. We then poll once, wait briefly and poll
             * again so short-lived jobs are caught here.
             */
            if (!check_jobs_internal(1)) {
                /*
                 * Give the resumed job a brief moment to terminate so we
                 * can print the completion message here rather than via the
                 * SIGCHLD handler after the prompt is shown.
                 */
                struct timespec ts = {0, 100000000}; /* 100ms */
                nanosleep(&ts, NULL);
                check_jobs_internal(1);
            }

            sigprocmask(SIG_SETMASK, &old, NULL);
            return 0;
        }
        curr = curr->next;
    }
    fprintf(stderr, "bg: job %d not found\n", id);
    return -1;
}

/*
 * Return the ID of the most recently started background job.
 * Returns 0 when no such job exists.
 */
int get_last_job_id(void) {
    return last_bg_id;
}

