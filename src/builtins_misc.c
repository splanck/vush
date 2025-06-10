/*
 * Miscellaneous builtin commands
 *
 * This file gathers builtins that don't fit the alias, variable,
 * file system or job-control groups.  Keeping them here avoids
 * cluttering those more focused modules.
 *
 * Some helpers, such as `source` and `eval`, invoke the parser and
 * executor directly so they behave like normal command evaluation.
 */
#define _GNU_SOURCE
#include "builtins.h"
#include "history.h"
#include "parser.h"
#include "execute.h"
#include "util.h"
#include "scriptargs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <ctype.h>
#include <sys/stat.h>
#include <fnmatch.h>
#include <stdint.h>
#include <time.h>

extern int last_status;
extern FILE *parse_input;
#include <sys/wait.h>
#include <signal.h>

char *trap_cmds[NSIG];
extern void trap_handler(int);

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

/* Display the command history or modify it with -c to clear or -d N to
 * delete a specific entry. */
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
    FILE *input = fopen(args[1], "r");
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
    if (!args[1]) {
        fprintf(stderr, "usage: command name [args...]\n");
        return 1;
    }
    pid_t pid = fork();
    if (pid == 0) {
        execvp(args[1], &args[1]);
        perror(args[1]);
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


/* Run a command and report the elapsed real time. */
int builtin_time(char **args) {
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
    printf("  test EXPR ([ EXPR ])  Evaluate a test expression\n");
    printf("  eval WORDS  Concatenate arguments and execute the result\n");
    printf("  exec CMD [ARGS]  Replace the shell with CMD\n");
    printf("  source FILE [ARGS...] (. FILE [ARGS...])\n");
    printf("  help       Display this help message\n");
    return 1;
}

/* POSIX test/['[' ] builtin for evaluating conditional expressions. */
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
        else if (strcmp(av[0], "-e") == 0) {
            struct stat st;
            res = stat(av[1], &st) == 0 ? 0 : 1;
        } else if (strcmp(av[0], "-f") == 0) {
            struct stat st;
            res = stat(av[1], &st) == 0 && S_ISREG(st.st_mode) ? 0 : 1;
        } else if (strcmp(av[0], "-d") == 0) {
            struct stat st;
            res = stat(av[1], &st) == 0 && S_ISDIR(st.st_mode) ? 0 : 1;
        } else if (strcmp(av[0], "-r") == 0) {
            res = access(av[1], R_OK) == 0 ? 0 : 1;
        } else if (strcmp(av[0], "-w") == 0) {
            res = access(av[1], W_OK) == 0 ? 0 : 1;
        } else if (strcmp(av[0], "-x") == 0) {
            res = access(av[1], X_OK) == 0 ? 0 : 1;
        }
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

/* [[ ... ]] conditional expression evaluator with pattern matching. */
int builtin_cond(char **args) {
    int count = 0;
    while (args[count]) count++;
    char **av = args + 1;
    count--;
    int res = 1;
    if (count == 1) {
        res = av[0][0] ? 0 : 1;
    } else if (count == 3) {
        if (strcmp(av[1], "==") == 0 || strcmp(av[1], "=") == 0) {
            if (strpbrk(av[2], "*?"))
                res = fnmatch(av[2], av[0], 0) == 0 ? 0 : 1;
            else
                res = strcmp(av[0], av[2]) == 0 ? 0 : 1;
        } else if (strcmp(av[1], "!=") == 0) {
            if (strpbrk(av[2], "*?"))
                res = fnmatch(av[2], av[0], 0) != 0 ? 0 : 1;
            else
                res = strcmp(av[0], av[2]) != 0 ? 0 : 1;
        }
    }
    last_status = res;
    return 1;
}

/* Show how each argument would be resolved: alias, function, builtin or file. */
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

/* Assign commands to run when specified signals are received. */
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

/* Signal a loop to terminate after the current iteration. */
int builtin_break(char **args)
{
    (void)args;
    loop_break = 1;
    return 1;
}

/* Skip directly to the next iteration of the innermost loop. */
int builtin_continue(char **args)
{
    (void)args;
    loop_continue = 1;
    return 1;
}

/* Display or modify the file creation mask. */
int builtin_umask(char **args)
{
    mode_t mask = umask(0);
    umask(mask);

    if (!args[1]) {
        printf("%04o\n", mask);
        return 1;
    }

    if (args[2]) {
        fprintf(stderr, "usage: umask [mode]\n");
        return 1;
    }

    errno = 0;
    char *end;
    long val = strtol(args[1], &end, 8);
    if (*end != '\0' || errno != 0 || val < 0 || val > 0777) {
        fprintf(stderr, "umask: invalid mode\n");
        return 1;
    }

    umask((mode_t)val);
    return 1;
}

static const char *next_format_spec(const char *p, char spec[32], char *conv)
{
    int si = 0;
    if (*p != '%') {
        spec[0] = '\0';
        *conv = '\0';
        return p;
    }

    spec[si++] = *p++;

    if (*p == '%') {
        spec[si++] = *p++;
        spec[si] = '\0';
        *conv = '%';
        return p;
    }

    while (*p && strchr("-+ #0", *p))
        spec[si++] = *p++;
    while (*p && isdigit((unsigned char)*p))
        spec[si++] = *p++;
    if (*p == '.') {
        spec[si++] = *p++;
        while (*p && isdigit((unsigned char)*p))
            spec[si++] = *p++;
    }
    if (strchr("hlLjzt", *p)) {
        spec[si++] = *p++;
        if ((*p == 'h' && spec[si-1] == 'h') ||
            (*p == 'l' && spec[si-1] == 'l'))
            spec[si++] = *p++;
    }

    if (*p) {
        *conv = *p;
        spec[si++] = *p++;
    } else {
        *conv = '\0';
    }

    spec[si] = '\0';
    return p;
}

/* Formatted printing similar to printf(1); stores result in last_status. */
int builtin_printf(char **args)
{
    const char *fmt = args[1] ? args[1] : "";
    int ai = 2;
    for (const char *p = fmt; *p; ) {
        if (*p != '%') {
            putchar(*p++);
            continue;
        }
        char spec[32];
        char conv;
        p = next_format_spec(p, spec, &conv);
        if (!conv)
            break;
        if (conv == '%') {
            putchar('%');
            continue;
        }

        char *arg = args[ai] ? args[ai] : "";
        switch (conv) {
        case 'd': case 'i':
            printf(spec, (long long)strtoll(arg, NULL, 0));
            if (args[ai]) ai++;
            break;
        case 'u': case 'o': case 'x': case 'X':
            printf(spec, (unsigned long long)strtoull(arg, NULL, 0));
            if (args[ai]) ai++;
            break;
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': case 'a': case 'A':
            printf(spec, strtod(arg, NULL));
            if (args[ai]) ai++;
            break;
        case 'c':
            printf(spec, arg[0]);
            if (args[ai]) ai++;
            break;
        case 's':
            printf(spec, arg);
            if (args[ai]) ai++;
            break;
        case 'p':
            printf(spec, (void *)(uintptr_t)strtoull(arg, NULL, 0));
            if (args[ai]) ai++;
            break;
        default:
            fputs(spec, stdout);
            break;
        }
    }
    fflush(stdout);
    last_status = 0;
    return 1;
}

