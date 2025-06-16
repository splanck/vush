/*
 * Core builtin commands
 *
 * This file holds basic builtins such as exit, :, true and false.
 */
#define _GNU_SOURCE
#include "builtins.h"
#include "vars.h"
#include "history.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "util.h"
#include <errno.h>

extern int last_status;

/* Exit the shell, freeing resources and using the provided status
 * or the status of the last command when none is given. */
int builtin_exit(char **args) {
    int status = last_status;
    if (args[1]) {
        char *end;
        errno = 0;
        long val = strtol(args[1], &end, 10);
        if (*end != '\0' || errno != 0) {
            return usage_error("exit [STATUS]");
        }
        status = (int)val;
    }
    delete_last_history_entry();
    run_exit_trap();
    free_aliases();
    free_functions();
    free_shell_vars();
    exit(status);
}

/* No-op builtin that always succeeds. */
int builtin_colon(char **args)
{
    (void)args;
    last_status = 0;
    return 1;
}

/* Always succeed and set status to 0. */
int builtin_true(char **args)
{
    (void)args;
    last_status = 0;
    return 1;
}

/* Always fail and set status to 1. */
int builtin_false(char **args)
{
    (void)args;
    last_status = 1;
    return 1;
}
