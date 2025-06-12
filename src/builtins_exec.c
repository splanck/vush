#define _GNU_SOURCE
#include "builtins.h"
#include "history.h"
#include "parser.h"
#include "execute.h"
#include "util.h"
#include "options.h"
#include "scriptargs.h"
#include "vars.h"
#include "hash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <sys/wait.h>

extern int last_status;
extern FILE *parse_input;

static int prepare_source_args(char **args, int *old_argc, char ***old_argv,
                               int *new_argc)
{
    *old_argc = script_argc;
    *old_argv = script_argv;

    int argc = 0;
    for (int i = 1; args[i]; i++)
        argc++;
    *new_argc = argc - 1;

    script_argc = *new_argc;
    script_argv = calloc(argc + 1, sizeof(char *));
    if (!script_argv) {
        script_argc = *old_argc;
        script_argv = *old_argv;
        return -1;
    }
    for (int i = 0; i < argc; i++) {
        script_argv[i] = strdup(args[i + 1]);
        if (!script_argv[i]) {
            for (int j = 0; j < i; j++)
                free(script_argv[j]);
            free(script_argv);
            script_argc = *old_argc;
            script_argv = *old_argv;
            return -1;
        }
    }
    script_argv[argc] = NULL;
    return 0;
}

static void restore_source_args(int old_argc, char **old_argv, int new_argc)
{
    for (int i = 0; i <= new_argc; i++)
        free(script_argv[i]);
    free(script_argv);
    script_argv = old_argv;
    script_argc = old_argc;
}

static void execute_source_file(FILE *input)
{
    char line[MAX_LINE];
    while (read_logical_line(input, line, sizeof(line))) {
        current_lineno++;
        if (opt_verbose)
            printf("%s\n", line);
        Command *cmds = parse_line(line);
        if (!cmds || !cmds->pipeline || !cmds->pipeline->argv[0]) {
            free_commands(cmds);
            continue;
        }
        add_history(line);
        CmdOp prev = OP_SEMI;
        for (Command *c = cmds; c; c = c->next) {
            int run = 1;
            if (c != cmds) {
                if (prev == OP_AND)
                    run = (last_status == 0);
                else if (prev == OP_OR)
                    run = (last_status != 0);
            }
            if (run)
                run_pipeline(c, line);
            prev = c->op;
        }
        free_commands(cmds);
    }
}

/* Read commands from a file and execute them in the current shell
 * environment. Additional arguments become script parameters. */
int builtin_source(char **args) {
    if (!args[1]) {
        fprintf(stderr, "usage: source file [args...]\n");
        return 1;
    }

    FILE *prev_input = parse_input;
    FILE *input = NULL;

    if (strchr(args[1], '/')) {
        input = fopen(args[1], "r");
    } else {
        const char *pathenv = getenv("PATH");
        if (pathenv && *pathenv) {
            char *paths = strdup(pathenv);
            if (paths) {
                char *save = NULL;
                for (char *p = strtok_r(paths, ":", &save); p; p = strtok_r(NULL, ":", &save)) {
                    const char *base = *p ? p : ".";
                    char full[PATH_MAX];
                    snprintf(full, sizeof(full), "%s/%s", base, args[1]);
                    input = fopen(full, "r");
                    if (input)
                        break;
                }
                free(paths);
            }
        }
    }

    if (!input) {
        perror(args[1]);
        return 1;
    }

    int old_argc, new_argc;
    char **old_argv;
    if (prepare_source_args(args, &old_argc, &old_argv, &new_argc) < 0) {
        fclose(input);
        return 1;
    }

    parse_input = input;
    execute_source_file(input);
    fclose(input);
    restore_source_args(old_argc, old_argv, new_argc);
    parse_input = prev_input;
    return 1;
}

/* Concatenate all arguments into a single command line and execute it. */
int builtin_eval(char **args) {
    if (!args[1])
        return 1;

    size_t len = 0;
    for (int i = 1; args[i]; i++)
        len += strlen(args[i]) + 1;

    char *line = malloc(len + 1);
    if (!line)
        return 1;
    line[0] = '\0';
    for (int i = 1; args[i]; i++) {
        strcat(line, args[i]);
        if (args[i + 1])
            strcat(line, " ");
    }

    Command *cmds = parse_line(line);
    if (cmds && cmds->pipeline && cmds->pipeline->argv[0])
        run_command_list(cmds, line);
    free_commands(cmds);
    free(line);
    return 1;
}

/* Replace the current shell with the specified program using execvp. */
int builtin_exec(char **args) {
    if (!args[1]) {
        fprintf(stderr, "usage: exec command [args...]\n");
        return 1;
    }
    execvp(args[1], &args[1]);
    perror(args[1]);
    return 1;
}

/* Execute a command ignoring any shell aliases or functions. */
int builtin_command(char **args) {
    const char fallback[] = "/bin:/usr/bin";
    int i = 1;
    int opt_v = 0, opt_V = 0, opt_p = 0;

    while (args[i] && args[i][0] == '-' && args[i][1]) {
        for (int k = 1; args[i][k]; k++) {
            if (args[i][k] == 'v') opt_v = 1;
            else if (args[i][k] == 'V') opt_V = 1;
            else if (args[i][k] == 'p') opt_p = 1;
            else break;
        }
        i++;
    }

    if (!args[i]) {
        fprintf(stderr, "usage: command [-p|-v|-V] name [args...]\n");
        return 1;
    }

    if (opt_v || opt_V) {
        int status = 0;
        for (; args[i]; i++) {
            const char *alias = get_alias(args[i]);
            if (alias) {
                if (opt_V)
                    printf("%s is an alias for '%s'\n", args[i], alias);
                else
                    printf("alias %s='%s'\n", args[i], alias);
                continue;
            }
            Command *fn = get_function(args[i]);
            if (fn) {
                if (opt_V)
                    printf("%s is a function\n", args[i]);
                else
                    printf("%s\n", args[i]);
                continue;
            }
            int is_builtin = 0;
            for (int j = 0; builtin_table[j].name; j++) {
                if (strcmp(args[i], builtin_table[j].name) == 0) {
                    if (opt_V)
                        printf("%s is a builtin\n", args[i]);
                    else
                        printf("%s\n", args[i]);
                    is_builtin = 1;
                    break;
                }
            }
            if (is_builtin)
                continue;
            const char *pathenv = opt_p ? fallback : getenv("PATH");
            if (!pathenv || !*pathenv)
                pathenv = fallback;
            char *paths = strdup(pathenv);
            if (!paths)
                continue;
            char *saveptr = NULL;
            char *dir = strtok_r(paths, ":", &saveptr);
            int found = 0;
            while (dir) {
                char full[PATH_MAX];
                snprintf(full, sizeof(full), "%s/%s", dir, args[i]);
                if (access(full, X_OK) == 0) {
                    if (opt_V)
                        printf("%s is %s\n", args[i], full);
                    else
                        printf("%s\n", full);
                    found = 1;
                    break;
                }
                dir = strtok_r(NULL, ":", &saveptr);
            }
            free(paths);
            if (!found) {
                if (opt_V)
                    printf("%s not found\n", args[i]);
                status = 1;
            }
        }
        last_status = status;
        return 1;
    }

    pid_t pid = fork();
    if (pid == 0) {
        if (opt_p)
            setenv("PATH", fallback, 1);
        execvp(args[i], &args[i]);
        perror(args[i]);
        _exit(127);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
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
