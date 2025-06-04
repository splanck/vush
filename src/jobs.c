/*
 * vush - a simple UNIX shell
 * Licensed under the GNU GPLv3.
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

void check_jobs(void) {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        printf("[vush] job %d finished\n", pid);
        remove_job(pid);
    }
}

void print_jobs(void) {
    Job *j = jobs;
    while (j) {
        printf("[%d] %d %s\n", j->id, j->pid, j->cmd);
        j = j->next;
    }
}

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

