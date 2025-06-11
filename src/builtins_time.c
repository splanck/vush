#define _GNU_SOURCE
#include "builtins.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <sys/times.h>
#include <sys/resource.h>
#include <unistd.h>

extern int last_status;

/* Run a command and report the elapsed real time. */
int builtin_time(char **args)
{
    if (!args[1]) {
        fprintf(stderr, "usage: time command [args...]\n");
        return 1;
    }

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    pid_t pid = fork();
    if (pid == 0) {
        execvp(args[1], &args[1]);
        perror(args[1]);
        _exit(127);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
        clock_gettime(CLOCK_MONOTONIC, &end);
        double elapsed = (end.tv_sec - start.tv_sec) +
                         (end.tv_nsec - start.tv_nsec) / 1e9;
        printf("real %.3f sec\n", elapsed);
        if (WIFEXITED(status))
            last_status = WEXITSTATUS(status);
        else if (WIFSIGNALED(status))
            last_status = 128 + WTERMSIG(status);
        else
            last_status = status;
        return 1;
    } else {
        perror("fork");
        last_status = 1;
        return 1;
    }
}

/* Print user/system CPU times for the shell and its children. */
int builtin_times(char **args)
{
    if (args[1]) {
        fprintf(stderr, "usage: times\n");
        last_status = 1;
        return 1;
    }

    struct tms t;
    if (times(&t) == (clock_t)-1) {
        perror("times");
        last_status = 1;
        return 1;
    }
    long hz = sysconf(_SC_CLK_TCK);
    if (hz <= 0)
        hz = 100;
    printf("%.2f %.2f\n%.2f %.2f\n",
           (double)t.tms_utime / hz,
           (double)t.tms_stime / hz,
           (double)t.tms_cutime / hz,
           (double)t.tms_cstime / hz);
    last_status = 0;
    return 1;
}

