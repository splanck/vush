/*
 * Job control builtins
 *
 * This file exposes the shell commands used to manipulate background
 * processes: `jobs`, `fg`, `bg` and `kill`.  These builtins are thin
 * wrappers around the helpers implemented in jobs.c, which maintains
 * the job list and performs the actual process management.  The
 * functions here simply parse command arguments and invoke those
 * helpers so the user can inspect, resume or signal running jobs.
 */
#define _GNU_SOURCE
#include "builtins.h"
#include "jobs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>

/* builtin_jobs - usage: jobs [-l|-p] [ID...] */
int builtin_jobs(char **args) {
    int mode = 0; /* 0=normal, 1=long, 2=pids */
    int idx = 1;
    for (; args[idx] && args[idx][0] == '-' && args[idx][1]; idx++) {
        if (strcmp(args[idx], "-l") == 0) {
            mode = 1;
        } else if (strcmp(args[idx], "-p") == 0) {
            mode = 2;
        } else {
            fprintf(stderr, "usage: jobs [-l|-p] [ID...]\n");
            return 1;
        }
    }

    int ids[64];
    int count = 0;
    for (; args[idx]; idx++) {
        ids[count++] = atoi(args[idx]);
    }

    print_jobs(mode, count, ids);
    return 1;
}

/* builtin_fg - usage: fg ID
 * Bring the specified job to the foreground using wait_job and return 1. */
int builtin_fg(char **args) {
    if (!args[1]) {
        fprintf(stderr, "usage: fg ID\n");
        return 1;
    }
    int id = atoi(args[1]);
    wait_job(id);
    return 1;
}

/* builtin_bg - usage: bg ID
 * Resume a stopped job in the background via bg_job and return 1. */
int builtin_bg(char **args) {
    if (!args[1]) {
        fprintf(stderr, "usage: bg ID\n");
        return 1;
    }
    int id = atoi(args[1]);
    bg_job(id);
    return 1;
}

/* builtin_kill - usage: kill [-SIGNAL] ID
 * Send a signal (default SIGTERM) to a job using kill_job and return 1. */
int builtin_kill(char **args) {
    if (!args[1]) {
        fprintf(stderr, "usage: kill [-SIGNAL] ID\n");
        return 1;
    }
    int sig = SIGTERM;
    int idx = 1;
    if (args[1][0] == '-') {
        sig = atoi(args[1] + 1);
        idx++;
    }
    if (!args[idx]) {
        fprintf(stderr, "usage: kill [-SIGNAL] ID\n");
        return 1;
    }
    int id = atoi(args[idx]);
    kill_job(id, sig);
    return 1;
}

/* builtin_wait - usage: wait [ID|PID]...
 * Wait for the given job IDs or process IDs to complete. Without
 * arguments wait for all child processes. */
int builtin_wait(char **args) {
    int i = 1;
    if (!args[1]) {
        int status;
        pid_t pid;
        while ((pid = wait(&status)) > 0)
            remove_job(pid);
        return 1;
    }
    for (; args[i]; i++) {
        char *end;
        long val = strtol(args[i], &end, 10);
        if (*end != '\0') {
            fprintf(stderr, "usage: wait [ID|PID]...\n");
            return 1;
        }
        pid_t pid = get_job_pid((int)val);
        int status;
        if (pid > 0) {
            waitpid(pid, &status, 0);
            remove_job(pid);
        } else if (waitpid((pid_t)val, &status, 0) == -1) {
            perror("wait");
        }
    }
    return 1;
}

