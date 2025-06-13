/*
 * Helpers for executing shell functions.
 *
 * Shell functions are stored as parsed command lists and are executed when
 * the executor encounters their name in a pipeline.  The caller passes the
 * function's command list along with the argument vector that invoked it.  The
 * arguments are duplicated so that `$0`, `$1`, etc. within the body expand
 * properly.  After the call returns these temporary values are discarded and
 * the previous script arguments are restored.
 */
#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>

#include "func_exec.h"
#include "execute.h"
#include "scriptargs.h"
#include "builtins.h"
#include "vars.h"

extern int last_status;

int func_return = 0;

/*
 * Execute a shell function.
 *
 * body - parsed list of commands forming the function body
 * args - argv array where args[0] is the function name and the rest are
 *        parameters passed by the caller
 *
 * The current script arguments are saved and replaced with duplicates of
 * 'args'. script_argc becomes the number of parameters so that positional
 * expansions like $1 work. run_command_list() then executes 'body'. After it
 * finishes, the original script_argv and script_argc values are restored.
 *
 * Returns the exit status of the function body or 1 if memory allocation
 * fails during setup.
 */
int run_function(Command *body, char **args) {
    int argc = 0;
    while (args[argc]) argc++;
    int old_argc = script_argc;
    char **old_argv = script_argv;
    script_argc = argc - 1;
    script_argv = calloc(argc + 1, sizeof(char *));
    getopts_pos = NULL; /* new $@ may invalidate getopts parsing state */
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
    push_local_scope();
    func_return = 0;
    run_command_list(body, NULL);
    pop_local_scope();
    for (int i = 0; i < argc; i++)
        free(script_argv[i]);
    free(script_argv);
    script_argv = old_argv;
    script_argc = old_argc;
    getopts_pos = NULL; /* freed argv, so clear getopts pointer */
    return last_status;
}
