/*
 * vush - a simple UNIX shell
 * Licensed under the BSD 2-Clause Simplified License.
 * Job control builtin commands.
 *
 * This module implements the shell commands used to manipulate
 * background processes: `jobs`, `fg`, `bg`, `kill` and `wait`.  The
 * builtins mainly parse their arguments and delegate the heavy lifting
 * to the helpers in jobs.c which maintain the job list and perform the
 * actual process management.
 */
#define _GNU_SOURCE
#include "builtins.h"
#include "jobs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include "signal_map.h"
#include <sys/wait.h>
#include <time.h>



void list_signals(void)
{
    int first = 1;
    for (int s = 1; s < NSIG; s++) {
        const char *n = name_from_sig(s);
        if (n) {
            if (!first)
                printf(" ");
            printf("%s", n);
            first = 0;
        }
    }
    printf("\n");
}

/* builtin_jobs - usage: jobs [-l|-p] [-r|-s] [-n] [ID...] */
int builtin_jobs(char **args) {
    int mode = 0;   /* 0=normal, 1=long, 2=pids */
    int filter = 0; /* 0=all, 1=running, 2=stopped */
    int changed = 0;
    int idx = 1;
    for (; args[idx] && args[idx][0] == '-' && args[idx][1]; idx++) {
        if (strcmp(args[idx], "-l") == 0) {
            mode = 1;
        } else if (strcmp(args[idx], "-p") == 0) {
            mode = 2;
        } else if (strcmp(args[idx], "-r") == 0) {
            filter = 1;
        } else if (strcmp(args[idx], "-s") == 0) {
            filter = 2;
        } else if (strcmp(args[idx], "-n") == 0) {
            changed = 1;
        } else {
            fprintf(stderr, "usage: jobs [-l|-p] [-r|-s] [-n] [ID...]\n");
            return 1;
        }
    }

    int ids[64];
    int count = 0;
    for (; args[idx]; idx++) {
        ids[count++] = atoi(args[idx]);
    }

    print_jobs(mode, filter, changed, count, ids);
    return 1;
}

/*
 * builtin_fg - usage: fg ID
 *
 * Move the given background job to the foreground.  When no ID is
 * supplied the most recently started job is used.  The shell waits for
 * the process to finish via wait_job() before returning 1 to keep the
 * interpreter running.
 */
int builtin_fg(char **args) {
    int id;
    if (!args[1]) {
        id = get_last_job_id();
        if (id == 0) {
            fprintf(stderr, "fg: no current job\n");
            return 1;
        }
    } else {
        id = parse_job_spec(args[1]);
        if (id < 0) {
            fprintf(stderr, "fg: %s: no such job\n", args[1]);
            return 1;
        }
    }
    wait_job(id);
    return 1;
}

/*
 * builtin_bg - usage: bg ID
 *
 * Resume a stopped job so it continues running in the background.
 * If no ID is provided the last job is assumed.  After calling
 * bg_job() the function briefly polls for completion so short-lived
 * tasks still print a notification before control returns.
 */
int builtin_bg(char **args) {
    int id;
    if (!args[1]) {
        id = get_last_job_id();
        if (id == 0) {
            fprintf(stderr, "bg: no current job\n");
            return 1;
        }
    } else {
        id = parse_job_spec(args[1]);
        if (id < 0) {
            fprintf(stderr, "bg: %s: no such job\n", args[1]);
            return 1;
        }
    }
    bg_job(id);

    /*
     * Poll briefly so a completion notice is printed when the resumed job
     * exits almost immediately.  This mirrors the behaviour used after
     * sending signals in builtin_kill and helps ensure the message appears
     * before control returns to the user.
     */
    struct timespec ts = {0, 10000000}; /* 10 ms */
    for (int i = 0; i < 100; i++) {
        int printed = check_jobs_internal(1);
        if (printed || get_job_pid(id) < 0)
            break;
        nanosleep(&ts, NULL);
    }
    return 1;
}

/* builtin_kill - usage: kill [-s SIGNAL|-SIGNAL] [-l] ID|PID...
 * Send a signal (default SIGTERM) to jobs or processes. */
int builtin_kill(char **args) {
    int sig = SIGTERM;
    int idx = 1;
    int list = 0;

    for (; args[idx] && args[idx][0] == '-' && args[idx][1]; idx++) {
        if (strcmp(args[idx], "-l") == 0) {
            list = 1;
        } else if (strcmp(args[idx], "-s") == 0) {
            idx++;
            if (!args[idx]) {
                fprintf(stderr, "usage: kill [-s SIGNAL|-SIGNAL] [-l] ID|PID...\n");
                return 1;
            }
            sig = sig_from_name(args[idx]);
            if (sig <= 0 || sig >= NSIG) {
                fprintf(stderr, "kill: invalid signal %s\n", args[idx]);
                return 1;
            }
        } else {
            int t = sig_from_name(args[idx] + 1);
            if (t <= 0 || t >= NSIG) {
                fprintf(stderr, "kill: invalid signal %s\n", args[idx] + 1);
                return 1;
            }
            sig = t;
        }
    }

    if (list && args[idx] && !args[idx + 1]) {
        int t = sig_from_name(args[idx]);
        if (t <= 0 || t >= NSIG) {
            fprintf(stderr, "kill: invalid signal %s\n", args[idx]);
            return 1;
        }
        const char *name = name_from_sig(t);
        if (name)
            printf("%s\n", name);
        else
            printf("%d\n", t);
        return 1;
    }

    if (list && !args[idx]) {
        list_signals();
        return 1;
    }

    if (!args[idx]) {
        fprintf(stderr, "usage: kill [-s SIGNAL|-SIGNAL] [-l] ID|PID...\n");
        return 1;
    }

    int wait_ids[64];
    int wait_count = 0;
    for (; args[idx]; idx++) {
        char *end;
        long val = strtol(args[idx], &end, 10);
        if (*end != '\0') {
            fprintf(stderr, "kill: invalid id %s\n", args[idx]);
            continue;
        }
        pid_t pid = get_job_pid((int)val);
        if (pid > 0) {
            kill_job((int)val, sig);
            wait_ids[wait_count++] = (int)val;
        } else if (kill((pid_t)val, sig) == -1) {
            perror("kill");
        }
    }
    /*
     * Repeatedly poll for completed jobs so the "job finished" notice is
     * printed before returning.  Call check_jobs_internal(1) so the
     * notification appears on a new line immediately, matching the
     * behaviour of other job-control builtins. Wait up to ~1s for the
     * targeted job to exit.
     */
    struct timespec ts = {0, 10000000}; /* 10 ms */
    for (int i = 0; i < 100; i++) {
        int printed = check_jobs_internal(1);
        int remaining = 0;
        for (int j = 0; j < wait_count; j++) {
            if (get_job_pid(wait_ids[j]) > 0) {
                remaining = 1;
                break;
            }
        }
        if (printed || !remaining)
            break;
        nanosleep(&ts, NULL);
    }
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

