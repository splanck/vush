/*
 * Miscellaneous builtin commands
 *
 * This file gathers builtins that don't fit the alias, variable,
 * file system or job-control groups.  Keeping them here avoids
 * cluttering those more focused modules.
 * Some helpers, such as `source` and `eval`, invoke the parser and
 * executor directly so they behave like normal command evaluation.
 */
#define _GNU_SOURCE
#include "builtins.h"
#include "hash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include "shell_state.h"
/* Manage or display command hash table. */
int builtin_hash(char **args) {
    int i = 1;
    int status = 0;
    if (args[i] && strcmp(args[i], "-r") == 0) {
        hash_clear();
        i++;
    }
    if (!args[i]) {
        hash_print();
        last_status = 0;
        return 1;
    }
    for (; args[i]; i++) {
        if (hash_add(args[i]) < 0) {
            fprintf(stderr, "%s: not found\n", args[i]);
            status = 1;
        }
    }
    last_status = status;
    return 1;
}





/* Print a usage summary of the available builtin commands. */
int builtin_help(char **args) {
    (void)args;
    printf("Built-in commands:\n");
    printf("  cd [dir]   Change the current directory ('cd -' toggles)\n");
    printf("  pushd DIR  Push current directory and switch to DIR\n");
    printf("  popd       Switch to directory from stack\n");
    printf("  printf FORMAT [ARGS]  Print formatted text\n");
    printf("  dirs       Display the directory stack\n");
    printf("  exit [status]  Exit the shell with optional status\n");
    printf("  :          Do nothing and return success\n");
    printf("  true       Return a successful status\n");
    printf("  false      Return a failure status\n");
    printf("  pwd        Print the current working directory\n");
    printf("  jobs       List running background jobs\n");
    printf("  fg ID      Wait for job ID in foreground\n");
    printf("  bg ID      Continue job ID in background\n");
    printf("  kill [-s SIG|-SIGNAL] [-l] ID|PID  Send a signal or list signals\n");
    printf("  export [-p|-n NAME] NAME[=VALUE]  Manage exported variables\n");
    printf("  readonly [-p] NAME[=VALUE]  Mark variable as read-only or list them\n");
    printf("  unset [-f|-v] NAME  Remove functions with -f or variables with -v\n");
    printf("  history [-c|-d NUM]   Show or modify command history\n");
    printf("  hash [-r] [name...]   Manage cached command paths\n");
    printf("  alias [-p] [NAME[=VALUE]]  Set or list aliases\n");
    printf("  unalias [-a] NAME   Remove alias(es)\n");
    printf("  read [-r] VAR...    Read a line into variables\n");
    printf("  return [status]     Return from a function\n");
    printf("  break      Exit the nearest loop\n");
    printf("  continue   Start next iteration of loop\n");
    printf("  shift      Shift positional parameters\n");
    printf("  getopts OPTSTRING VAR   Parse options from positional params\n");
    printf("  let EXPR  Evaluate arithmetic expression\n");
    printf("  set [-e|-u|-x] Toggle shell options\n");
    printf("  test EXPR ([ EXPR ])  Evaluate a test expression (!, -a, -o)\n");
    printf("  ulimit [-HS] [-a|-f|-n] [limit]  Display or set resource limits\n");
    printf("  eval WORDS  Concatenate arguments and execute the result\n");
    printf("  exec CMD [ARGS]  Replace the shell with CMD\n");
    printf("  source FILE [ARGS...] (. FILE [ARGS...])\n");
    printf("  help       Display this help message\n");
    return 1;
}


/* Show how each argument would be resolved: alias, function, builtin or file. */
int builtin_type(char **args) {
    int opt_t = 0;
    int i = 1;
    if (args[i] && strcmp(args[i], "-t") == 0) {
        opt_t = 1;
        i++;
    }
    if (!args[i]) {
        fprintf(stderr, "usage: type [-t] name...\n");
        return 1;
    }
    for (; args[i]; i++) {
        const char *alias = get_alias(args[i]);
        if (alias) {
            if (opt_t)
                printf("alias\n");
            else
                printf("%s is an alias for '%s'\n", args[i], alias);
            continue;
        }
        if (find_function(args[i])) {
            if (opt_t)
                printf("function\n");
            else
                printf("%s is a function\n", args[i]);
            continue;
        }
        int is_builtin = 0;
        for (int j = 0; builtin_table[j].name; j++) {
            if (strcmp(args[i], builtin_table[j].name) == 0) {
                if (opt_t)
                    printf("builtin\n");
                else
                    printf("%s is a builtin\n", args[i]);
                is_builtin = 1;
                break;
            }
        }
        if (is_builtin)
            continue;
        const char *pathenv = getenv("PATH");
        if (!pathenv)
            pathenv = "/bin:/usr/bin";
        char *paths = strdup(pathenv);
        if (!paths)
            continue;
        char *saveptr = NULL;
        char *dir = strtok_r(paths, ":", &saveptr);
        int found = 0;
        while (dir) {
            const char *d = *dir ? dir : ".";
            size_t len = strlen(d) + strlen(args[i]) + 2;
            char *full = malloc(len);
            if (!full)
                break;
            snprintf(full, len, "%s/%s", d, args[i]);
            if (access(full, X_OK) == 0) {
                if (opt_t)
                    printf("file\n");
                else
                    printf("%s is %s\n", args[i], full);
                found = 1;
                free(full);
                break;
            }
            free(full);
            dir = strtok_r(NULL, ":", &saveptr);
        }
        free(paths);
        if (!found) {
            if (opt_t)
                printf("not found\n");
            else
                printf("%s not found\n", args[i]);
        }
    }
    return 1;
}

