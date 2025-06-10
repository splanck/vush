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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>

typedef struct Job {
    int id;
    pid_t pid;
    char cmd[MAX_LINE];
    struct Job *next;
} Job;

static Job *jobs = NULL;
static int next_job_id = 1;

/*
 * Record a child process that was started in the background.
 * Called by the executor whenever a command is launched with '&'.
 */
void add_job(pid_t pid, const char *cmd) {
    Job *job = malloc(sizeof(Job));
    if (!job) return;
    job->id = next_job_id++;
    job->pid = pid;
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
void check_jobs(void) {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        Job *curr = jobs;
        while (curr && curr->pid != pid)
            curr = curr->next;
        if (curr)
            printf("[vush] job %d (%s) finished\n", curr->id, curr->cmd);
        else
            printf("[vush] job %d finished\n", pid);
        remove_job(pid);
    }
}

/*
 * Display the list of active background jobs.  Used by the `jobs`
 * builtin to show the user what is currently running.
 */
void print_jobs(void) {
    Job *j = jobs;
    while (j) {
        printf("[%d] %d %s\n", j->id, j->pid, j->cmd);
        j = j->next;
    }
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
            if (kill(curr->pid, SIGCONT) != 0) {
                perror("bg");
                return -1;
            }
            return 0;
        }
        curr = curr->next;
    }
    fprintf(stderr, "bg: job %d not found\n", id);
    return -1;
}

