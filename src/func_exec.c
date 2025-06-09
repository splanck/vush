/*
 * Helpers for executing shell functions.
 */
#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>

#include "func_exec.h"
#include "execute.h"
#include "scriptargs.h"

extern int last_status;

int func_return = 0;

int run_function(Command *body, char **args) {
    int argc = 0;
    while (args[argc]) argc++;
    int old_argc = script_argc;
    char **old_argv = script_argv;
    script_argc = argc - 1;
    script_argv = calloc(argc + 1, sizeof(char *));
    if (!script_argv) {
        script_argc = old_argc;
        script_argv = old_argv;
        return 1;
    }
    for (int i = 0; i < argc; i++) {
        script_argv[i] = strdup(args[i]);
        if (!script_argv[i]) {
            for (int j = 0; j < i; j++)
                free(script_argv[j]);
            free(script_argv);
            script_argv = old_argv;
            script_argc = old_argc;
            return 1;
        }
    }
    script_argv[argc] = NULL;
    func_return = 0;
    run_command_list(body, NULL);
    for (int i = 0; i < argc; i++)
        free(script_argv[i]);
    free(script_argv);
    script_argv = old_argv;
    script_argc = old_argc;
    return last_status;
}
