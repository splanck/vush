/* Miscellaneous builtin commands */
#define _GNU_SOURCE
#include "builtins.h"
#include "history.h"
#include "parser.h"
#include "execute.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <linux/limits.h>
#include <ctype.h>

extern int last_status;
extern FILE *parse_input;
#include <sys/wait.h>
#include <signal.h>

char *trap_cmds[NSIG];
extern void trap_handler(int);

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
    free_aliases();
    free_functions();
    free_shell_vars();
    exit(status);
}

int builtin_history(char **args) {
    if (args[1]) {
        if (strcmp(args[1], "-c") == 0 && !args[2]) {
            clear_history();
            return 1;
        } else if (strcmp(args[1], "-d") == 0 && args[2] && !args[3]) {
            int id = atoi(args[2]);
            delete_history_entry(id);
            return 1;
        } else {
            fprintf(stderr, "usage: history [-c|-d NUMBER]\n");
            return 1;
        }
    }
    print_history();
    return 1;
}

int builtin_source(char **args) {
    if (!args[1]) {
        fprintf(stderr, "usage: source file\n");
        return 1;
    }

    FILE *prev_input = parse_input;
    FILE *input = fopen(args[1], "r");
    if (!input) {
        perror(args[1]);
        return 1;
    }

    parse_input = input;
    char line[MAX_LINE];
    while (read_logical_line(input, line, sizeof(line))) {
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
    fclose(input);
    parse_input = prev_input;
    return 1;
}

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

int builtin_help(char **args) {
    (void)args;
    printf("Built-in commands:\n");
    printf("  cd [dir]   Change the current directory ('cd -' toggles)\n");
    printf("  pushd DIR  Push current directory and switch to DIR\n");
    printf("  popd       Switch to directory from stack\n");
    printf("  dirs       Display the directory stack\n");
    printf("  exit [status]  Exit the shell with optional status\n");
    printf("  pwd        Print the current working directory\n");
    printf("  jobs       List running background jobs\n");
    printf("  fg ID      Wait for job ID in foreground\n");
    printf("  bg ID      Continue job ID in background\n");
    printf("  kill [-SIGNAL] ID   Send a signal to job ID\n");
    printf("  export NAME=value   Set an environment variable\n");
    printf("  unset NAME          Remove an environment variable\n");
    printf("  history [-c|-d NUM]   Show or modify command history\n");
    printf("  alias NAME=VALUE    Set an alias\n");
    printf("  unalias NAME        Remove an alias\n");
    printf("  read [-r] VAR...    Read a line into variables\n");
    printf("  return [status]     Return from a function\n");
    printf("  break      Exit the nearest loop\n");
    printf("  continue   Start next iteration of loop\n");
    printf("  shift      Shift positional parameters\n");
    printf("  getopts OPTSTRING VAR   Parse options from positional params\n");
    printf("  let EXPR  Evaluate arithmetic expression\n");
    printf("  set [-e|-u|-x] Toggle shell options\n");
    printf("  eval WORDS  Concatenate arguments and execute the result\n");
    printf("  source FILE (. FILE)   Execute commands from FILE\n");
    printf("  help       Display this help message\n");
    return 1;
}

int builtin_test(char **args) {
    int count = 0;
    while (args[count]) count++;
    if (strcmp(args[0], "[") == 0) {
        if (count < 2 || strcmp(args[count-1], "]") != 0) {
            fprintf(stderr, "[: missing ]\n");
            last_status = 1;
            return 1;
        }
        args[count-1] = NULL;
        count--;
    }
    char **av = args + 1;
    count--;
    int res = 1;
    if (count == 0) {
        res = 1;
    } else if (count == 1) {
        res = av[0][0] ? 0 : 1;
    } else if (count == 2) {
        if (strcmp(av[0], "-n") == 0)
            res = av[1][0] ? 0 : 1;
        else if (strcmp(av[0], "-z") == 0)
            res = av[1][0] ? 1 : 0;
    } else if (count == 3) {
        if (strcmp(av[1], "=") == 0)
            res = strcmp(av[0], av[2]) == 0 ? 0 : 1;
        else if (strcmp(av[1], "!=") == 0)
            res = strcmp(av[0], av[2]) != 0 ? 0 : 1;
        else if (strcmp(av[1], "-eq") == 0)
            res = (atoi(av[0]) == atoi(av[2])) ? 0 : 1;
        else if (strcmp(av[1], "-ne") == 0)
            res = (atoi(av[0]) != atoi(av[2])) ? 0 : 1;
        else if (strcmp(av[1], "-gt") == 0)
            res = (atoi(av[0]) > atoi(av[2])) ? 0 : 1;
        else if (strcmp(av[1], "-lt") == 0)
            res = (atoi(av[0]) < atoi(av[2])) ? 0 : 1;
        else if (strcmp(av[1], "-ge") == 0)
            res = (atoi(av[0]) >= atoi(av[2])) ? 0 : 1;
        else if (strcmp(av[1], "-le") == 0)
            res = (atoi(av[0]) <= atoi(av[2])) ? 0 : 1;
    }
    last_status = res;
    return 1;
}

int builtin_type(char **args) {
    if (!args[1]) {
        fprintf(stderr, "usage: type name...\n");
        return 1;
    }
    for (int i = 1; args[i]; i++) {
        const char *alias = get_alias(args[i]);
        if (alias) {
            printf("%s is an alias for '%s'\n", args[i], alias);
            continue;
        }
        Command *fn = get_function(args[i]);
        if (fn) {
            printf("%s is a function\n", args[i]);
            continue;
        }
        int is_builtin = 0;
        for (int j = 0; builtin_table[j].name; j++) {
            if (strcmp(args[i], builtin_table[j].name) == 0) {
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
            char full[PATH_MAX];
            snprintf(full, sizeof(full), "%s/%s", dir, args[i]);
            if (access(full, X_OK) == 0) {
                printf("%s is %s\n", args[i], full);
                found = 1;
                break;
            }
            dir = strtok_r(NULL, ":", &saveptr);
        }
        free(paths);
        if (!found)
            printf("%s not found\n", args[i]);
    }
    return 1;
}

static int sig_from_name(const char *name)
{
    if (!name || !*name)
        return -1;
    if (isdigit((unsigned char)name[0]))
        return atoi(name);
    if (strncasecmp(name, "SIG", 3) == 0)
        name += 3;
    struct { const char *n; int v; } map[] = {
        {"INT", SIGINT}, {"TERM", SIGTERM}, {"HUP", SIGHUP},
#ifdef SIGQUIT
        {"QUIT", SIGQUIT},
#endif
#ifdef SIGUSR1
        {"USR1", SIGUSR1},
#endif
#ifdef SIGUSR2
        {"USR2", SIGUSR2},
#endif
#ifdef SIGCHLD
        {"CHLD", SIGCHLD},
#endif
#ifdef SIGCONT
        {"CONT", SIGCONT},
#endif
#ifdef SIGSTOP
        {"STOP", SIGSTOP},
#endif
#ifdef SIGALRM
        {"ALRM", SIGALRM},
#endif
        {NULL, 0}
    };
    for (int i = 0; map[i].n; i++) {
        if (strcasecmp(name, map[i].n) == 0)
            return map[i].v;
    }
    return -1;
}

int builtin_trap(char **args)
{
    if (!args[1]) {
        fprintf(stderr, "usage: trap [command] SIGNAL...\n");
        return 1;
    }

    char *cmd = NULL;
    int idx = 1;
    if (args[2]) {
        cmd = args[1];
        idx = 2;
    }

    for (int i = idx; args[i]; i++) {
        int sig = sig_from_name(args[i]);
        if (sig <= 0 || sig >= NSIG) {
            fprintf(stderr, "trap: invalid signal %s\n", args[i]);
            continue;
        }
        free(trap_cmds[sig]);
        trap_cmds[sig] = cmd ? strdup(cmd) : NULL;

        struct sigaction sa;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sa.sa_handler = cmd ? trap_handler : SIG_DFL;
        sigaction(sig, &sa, NULL);
    } 
    return 1;
}

int builtin_break(char **args)
{
    (void)args;
    loop_break = 1;
    return 1;
}

int builtin_continue(char **args)
{
    (void)args;
    loop_continue = 1;
    return 1;
}

