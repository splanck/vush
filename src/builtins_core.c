/*
 * vush - a simple UNIX shell
 * Licensed under the BSD 2-Clause Simplified License.
 * Core builtin commands like exit.
 */

/*
 * Core builtin commands
 *
 * This file holds basic builtins such as exit, :, true and false.
 */
#define _GNU_SOURCE
#include "builtins.h"
#include "shell_state.h"
#include "vars.h"
#include "history.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>


/* Exit the shell, freeing resources and using the provided status
 * or the status of the last command when none is given. */
int builtin_exit(char **args) {
    int status = last_status;
    if (args[1]) {
        char *end;
        errno = 0;
        long val = strtol(args[1], &end, 10);
        if (*end != '\0' || errno != 0) {
            fprintf(stderr, "usage: exit [STATUS]\n");
            return 1;
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
