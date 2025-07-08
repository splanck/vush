/*
 * vush - a simple UNIX shell
 * Licensed under the BSD 2-Clause Simplified License.
 * Timing builtin for commands.
 */

#define _GNU_SOURCE
#include "builtins.h"
#include "builtin_options.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include "shell_state.h"
#include <string.h>
#include <sys/times.h>
#include <unistd.h>

struct run_data { char **argv; };

static int exec_cmd(void *d);

static int do_time(int posix, int (*func)(void *), void *data)
{
    struct timespec start, end;
    struct tms t0, t1;
    long hz = sysconf(_SC_CLK_TCK);
    if (hz <= 0)
        hz = 100;
    clock_gettime(CLOCK_MONOTONIC, &start);
    times(&t0);
    int status = func(data);
    times(&t1);
    clock_gettime(CLOCK_MONOTONIC, &end);

    double real = (end.tv_sec - start.tv_sec) +
                  (end.tv_nsec - start.tv_nsec) / 1e9;
    double user = ((t1.tms_utime - t0.tms_utime) +
                   (t1.tms_cutime - t0.tms_cutime)) / (double)hz;
    double sys = ((t1.tms_stime - t0.tms_stime) +
                  (t1.tms_cstime - t0.tms_cstime)) / (double)hz;

    if (posix)
        printf("real %.3f\nuser %.3f\nsys %.3f\n", real, user, sys);
    else
        printf("real %.3f sec\n", real);

    return status;
}

/* Helper for timing an arbitrary function used by the time builtin. */
int builtin_time_callback(int (*func)(void *), void *data, int posix)
{
    return do_time(posix, func, data);
}

static int exec_cmd(void *d)
{
    char **av = ((struct run_data *)d)->argv;
    pid_t pid = fork();
    if (pid == 0) {
        execvp(av[0], av);
        perror(av[0]);
        _exit(127);
    } else if (pid > 0) {
        int status;
        if (waitpid(pid, &status, 0) == -1) {
            perror("waitpid");
            return 1;
        }
        if (WIFEXITED(status))
            return WEXITSTATUS(status);
        else if (WIFSIGNALED(status))
            return 128 + WTERMSIG(status);
        else
            return status;
    } else {
        perror("fork");
        return 1;
    }
}

/* Run a command and report the elapsed real time. */
int builtin_time(char **args)
{
    int posix = 0;
    int idx = parse_builtin_options(args, "p", &posix);
    if (idx < 0 || !args[idx]) {
        fprintf(stderr, "usage: time [-p] command [args...]\n");
        return 1;
    }

    struct run_data rd = { &args[idx] };
    int status = do_time(posix, exec_cmd, &rd);
    last_status = status;
    return 1;
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

