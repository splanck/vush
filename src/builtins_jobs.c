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
#include <signal.h>

/* builtin_jobs - usage: jobs
 * Print the list of background jobs recorded by jobs.c and return 1. */
int builtin_jobs(char **args) {
    (void)args;
    print_jobs();
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

