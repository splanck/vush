#define _GNU_SOURCE
#include "builtins.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <string.h>
#include <sys/times.h>
#include <sys/resource.h>
#include <unistd.h>

extern int last_status;

/* Run a command and report the elapsed real time. */
int builtin_time(char **args)
{
    int posix = 0;
    int idx = 1;

    if (args[idx] && strcmp(args[idx], "-p") == 0) {
        posix = 1;
        idx++;
    }

    if (!args[idx]) {
        return usage_error("time [-p] command [args...]");
    }

    struct timespec start, end;
    struct rusage ru;
    clock_gettime(CLOCK_MONOTONIC, &start);
    pid_t pid = fork();
    if (pid == 0) {
        execvp(args[idx], &args[idx]);
        perror(args[idx]);
        _exit(127);
    } else if (pid > 0) {
        int status;
        if (wait4(pid, &status, 0, &ru) == -1) {
            perror("wait4");
            last_status = 1;
            return 1;
        }
        clock_gettime(CLOCK_MONOTONIC, &end);

        double real = (end.tv_sec - start.tv_sec) +
                      (end.tv_nsec - start.tv_nsec) / 1e9;
        double user = ru.ru_utime.tv_sec + ru.ru_utime.tv_usec / 1e6;
        double sys = ru.ru_stime.tv_sec + ru.ru_stime.tv_usec / 1e6;

        if (posix)
            printf("real %.3f\nuser %.3f\nsys %.3f\n", real, user, sys);
        else
            printf("real %.3f sec\n", real);

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
        last_status = 1;
        return usage_error("times");
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

