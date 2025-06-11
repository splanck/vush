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
#include <ctype.h>
#include <sys/stat.h>
#include <fnmatch.h>
#include <stdint.h>
#include <sys/resource.h>

extern int last_status;
extern FILE *parse_input;
#include <sys/wait.h>
extern void run_exit_trap(void);

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

/* Print arguments separated by spaces. Supports -n to suppress the
 * trailing newline and -e to interpret common backslash escapes. */
int builtin_echo(char **args)
{
    int newline = 1;
    int interpret = 0;
    int i = 1;
    for (; args[i] && args[i][0] == '-' && args[i][1]; i++) {
        if (strcmp(args[i], "-n") == 0) {
            newline = 0;
            continue;
        }
        if (strcmp(args[i], "-e") == 0) {
            interpret = 1;
            continue;
        }
        break;
    }

    for (; args[i]; i++) {
        if (i > 1 && args[i-1])
            putchar(' ');
        const char *s = args[i];
        if (interpret) {
            for (const char *p = s; *p; p++) {
                if (*p == '\\' && p[1]) {
                    p++;
                    switch (*p) {
                    case 'n': putchar('\n'); break;
                    case 't': putchar('\t'); break;
                    case 'r': putchar('\r'); break;
                    case 'b': putchar('\b'); break;
                    case 'a': putchar('\a'); break;
                    case 'f': putchar('\f'); break;
                    case 'v': putchar('\v'); break;
                    case '\\': putchar('\\'); break;
                    default: putchar('\\'); putchar(*p); break;
                    }
                } else {
                    putchar(*p);
                }
            }
        } else {
            fputs(s, stdout);
        }
    }
    if (newline)
        putchar('\n');
    fflush(stdout);
    last_status = 0;
    return 1;
}




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
    printf("  unset NAME          Remove an environment variable\n");
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
    printf("  test EXPR ([ EXPR ])  Evaluate a test expression\n");
    printf("  ulimit [-HS] [-a|-f|-n] [limit]  Display or set resource limits\n");
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
        } else if (strcmp(av[0], "-b") == 0) {
            struct stat st;
            res = stat(av[1], &st) == 0 && S_ISBLK(st.st_mode) ? 0 : 1;
        } else if (strcmp(av[0], "-c") == 0) {
            struct stat st;
            res = stat(av[1], &st) == 0 && S_ISCHR(st.st_mode) ? 0 : 1;
        } else if (strcmp(av[0], "-p") == 0) {
            struct stat st;
            res = stat(av[1], &st) == 0 && S_ISFIFO(st.st_mode) ? 0 : 1;
        } else if (strcmp(av[0], "-S") == 0) {
            struct stat st;
            res = stat(av[1], &st) == 0 && S_ISSOCK(st.st_mode) ? 0 : 1;
        } else if (strcmp(av[0], "-h") == 0 || strcmp(av[0], "-L") == 0) {
            struct stat st;
            res = lstat(av[1], &st) == 0 && S_ISLNK(st.st_mode) ? 0 : 1;
        } else if (strcmp(av[0], "-s") == 0) {
            struct stat st;
            res = stat(av[1], &st) == 0 && st.st_size > 0 ? 0 : 1;
        } else if (strcmp(av[0], "-g") == 0) {
            struct stat st;
            res = stat(av[1], &st) == 0 && (st.st_mode & S_ISGID) ? 0 : 1;
        } else if (strcmp(av[0], "-u") == 0) {
            struct stat st;
            res = stat(av[1], &st) == 0 && (st.st_mode & S_ISUID) ? 0 : 1;
        } else if (strcmp(av[0], "-k") == 0) {
            struct stat st;
            res = stat(av[1], &st) == 0 && (st.st_mode & S_ISVTX) ? 0 : 1;
        } else if (strcmp(av[0], "-O") == 0) {
            struct stat st;
            res = stat(av[1], &st) == 0 && st.st_uid == geteuid() ? 0 : 1;
        } else if (strcmp(av[0], "-G") == 0) {
            struct stat st;
            res = stat(av[1], &st) == 0 && st.st_gid == getegid() ? 0 : 1;
        } else if (strcmp(av[0], "-t") == 0) {
            int fd = atoi(av[1]);
            res = isatty(fd) ? 0 : 1;
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
        else if (strcmp(av[1], "-nt") == 0) {
            struct stat st1, st2;
            if (stat(av[0], &st1) == 0 && stat(av[2], &st2) == 0)
                res = (st1.st_mtime > st2.st_mtime) ? 0 : 1;
            else
                res = 1;
        } else if (strcmp(av[1], "-ot") == 0) {
            struct stat st1, st2;
            if (stat(av[0], &st1) == 0 && stat(av[2], &st2) == 0)
                res = (st1.st_mtime < st2.st_mtime) ? 0 : 1;
            else
                res = 1;
        } else if (strcmp(av[1], "-ef") == 0) {
            struct stat st1, st2;
            if (stat(av[0], &st1) == 0 && stat(av[2], &st2) == 0)
                res = (st1.st_dev == st2.st_dev && st1.st_ino == st2.st_ino) ? 0 : 1;
            else
                res = 1;
        }
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


/* Display or modify the file creation mask. */
/* Helper to print a mask in symbolic form like u=rwx,g=rx,o=rx. */
static void print_symbolic_umask(mode_t mask)
{
    mode_t perms = (~mask) & 0777;
    char buf[32];
    char *p = buf;
    *p++ = 'u'; *p++ = '=';
    if (perms & 0400) *p++ = 'r';
    if (perms & 0200) *p++ = 'w';
    if (perms & 0100) *p++ = 'x';
    *p++ = ',';
    *p++ = 'g'; *p++ = '=';
    if (perms & 0040) *p++ = 'r';
    if (perms & 0020) *p++ = 'w';
    if (perms & 0010) *p++ = 'x';
    *p++ = ',';
    *p++ = 'o'; *p++ = '=';
    if (perms & 0004) *p++ = 'r';
    if (perms & 0002) *p++ = 'w';
    if (perms & 0001) *p++ = 'x';
    *p = '\0';
    printf("%s\n", buf);
}

/* Parse a symbolic mode string like u=rwx,g=rx,o= and return the
   corresponding mask. Returns 0 on success, -1 on error. */
static int parse_symbolic_umask(const char *str, mode_t *out)
{
    char *copy = strdup(str);
    if (!copy)
        return -1;
    mode_t perms = 0;
    int fields = 0;
    char *saveptr;
    char *tok = strtok_r(copy, ",", &saveptr);
    while (tok) {
        if (tok[0] && tok[1] == '=') {
            mode_t bits = 0;
            for (char *p = tok + 2; *p; p++) {
                if (*p == 'r')
                    bits |= 4;
                else if (*p == 'w')
                    bits |= 2;
                else if (*p == 'x')
                    bits |= 1;
                else {
                    free(copy);
                    return -1;
                }
            }
            switch (tok[0]) {
                case 'u':
                    if (fields & 1) { free(copy); return -1; }
                    perms |= bits << 6; fields |= 1; break;
                case 'g':
                    if (fields & 2) { free(copy); return -1; }
                    perms |= bits << 3; fields |= 2; break;
                case 'o':
                    if (fields & 4) { free(copy); return -1; }
                    perms |= bits; fields |= 4; break;
                default:
                    free(copy);
                    return -1;
            }
        } else {
            free(copy);
            return -1;
        }
        tok = strtok_r(NULL, ",", &saveptr);
    }
    free(copy);
    if (fields != 7)
        return -1;
    *out = (~perms) & 0777;
    return 0;
}

int builtin_umask(char **args)
{
    mode_t mask = umask(0);
    umask(mask);

    int symbolic = 0;
    int idx = 1;
    if (args[idx] && strcmp(args[idx], "-S") == 0) {
        symbolic = 1;
        idx++;
    }

    if (!args[idx]) {
        if (symbolic)
            print_symbolic_umask(mask);
        else
            printf("%04o\n", mask);
        return 1;
    }

    if (args[idx+1]) {
        fprintf(stderr, "usage: umask [-S] [mode]\n");
        return 1;
    }

    errno = 0;
    char *end;
    long val = strtol(args[idx], &end, 8);
    mode_t newmask;
    if (*end == '\0' && errno == 0 && val >= 0 && val <= 0777) {
        newmask = (mode_t)val;
    } else if (parse_symbolic_umask(args[idx], &newmask) == 0) {
        /* parsed successfully */
    } else {
        fprintf(stderr, "umask: invalid mode\n");
        return 1;
    }

    umask(newmask);
    if (symbolic)
        print_symbolic_umask(newmask);
    return 1;
}

/* Display or set resource limits. Supports -a and several limit flags */
int builtin_ulimit(char **args)
{
    struct {
        char opt;
        int r;
    } const map[] = {
        {'c', RLIMIT_CORE},
        {'d', RLIMIT_DATA},
        {'f', RLIMIT_FSIZE},
        {'n', RLIMIT_NOFILE},
        {'s', RLIMIT_STACK},
        {'t', RLIMIT_CPU},
        {'v', RLIMIT_AS},
        {0, 0}
    };

    int resource = RLIMIT_FSIZE;
    int show_all = 0;
    int hard = 0; /* default to soft limit */
    int i = 1;
    for (; args[i] && args[i][0] == '-'; i++) {
        if (strcmp(args[i], "-a") == 0) {
            show_all = 1;
        } else if (strcmp(args[i], "-H") == 0) {
            hard = 1;
        } else if (strcmp(args[i], "-S") == 0) {
            hard = 0;
        } else if (args[i][1] && !args[i][2]) {
            int found = 0;
            for (int m = 0; map[m].opt; m++) {
                if (args[i][1] == map[m].opt) {
                    resource = map[m].r;
                    found = 1;
                    break;
                }
            }
            if (!found) {
                fprintf(stderr, "usage: ulimit [-HS] [-a|-c|-d|-f|-n|-s|-t|-v] [limit]\n");
                return 1;
            }
        } else {
            fprintf(stderr, "usage: ulimit [-HS] [-a|-c|-d|-f|-n|-s|-t|-v] [limit]\n");
            return 1;
        }
    }

    if (show_all) {
        if (args[i]) {
            fprintf(stderr, "usage: ulimit [-HS] [-a|-c|-d|-f|-n|-s|-t|-v] [limit]\n");
            return 1;
        }
        struct rlimit rl;
        for (int m = 0; map[m].opt; m++) {
            if (getrlimit(map[m].r, &rl) == 0) {
                rlim_t val = hard ? rl.rlim_max : rl.rlim_cur;
                if (val == RLIM_INFINITY)
                    printf("-%c unlimited\n", map[m].opt);
                else
                    printf("-%c %llu\n", map[m].opt,
                           (unsigned long long)val);
            }
        }
        last_status = 0;
        return 1;
    }

    if (!args[i]) {
        struct rlimit rl;
        if (getrlimit(resource, &rl) != 0) {
            perror("ulimit");
            last_status = 1;
        } else {
            rlim_t val = hard ? rl.rlim_max : rl.rlim_cur;
            if (val == RLIM_INFINITY)
                printf("unlimited\n");
            else
                printf("%llu\n",
                       (unsigned long long)val);
            last_status = 0;
        }
        return 1;
    }

    if (args[i+1]) {
        fprintf(stderr, "usage: ulimit [-HS] [-a|-c|-d|-f|-n|-s|-t|-v] [limit]\n");
        return 1;
    }

    errno = 0;
    char *end;
    unsigned long long val = strtoull(args[i], &end, 10);
    if (*end != '\0' || errno != 0) {
        fprintf(stderr, "ulimit: invalid limit\n");
        return 1;
    }

    struct rlimit rl;
    if (getrlimit(resource, &rl) != 0) {
        perror("ulimit");
        last_status = 1;
        return 1;
    }
    if (hard)
        rl.rlim_max = val;
    else
        rl.rlim_cur = val;
    if (setrlimit(resource, &rl) != 0) {
        perror("ulimit");
        last_status = 1;
    } else {
        last_status = 0;
    }
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
        case 'b': {
            size_t len = strlen(arg);
            char *buf = malloc(len + 1);
            if (!buf) {
                perror("printf");
                last_status = 1;
                return 1;
            }
            char *bp = buf;
            for (const char *p2 = arg; *p2; p2++) {
                if (*p2 == '\\' && p2[1]) {
                    p2++;
                    switch (*p2) {
                    case 'n': *bp++ = '\n'; break;
                    case 't': *bp++ = '\t'; break;
                    case 'r': *bp++ = '\r'; break;
                    case 'b': *bp++ = '\b'; break;
                    case 'a': *bp++ = '\a'; break;
                    case 'f': *bp++ = '\f'; break;
                    case 'v': *bp++ = '\v'; break;
                    case '\\': *bp++ = '\\'; break;
                    default: *bp++ = '\\'; *bp++ = *p2; break;
                    }
                } else {
                    *bp++ = *p2;
                }
            }
            *bp = '\0';
            spec[strlen(spec) - 1] = 's';
            printf(spec, buf);
            free(buf);
            if (args[ai]) ai++;
            break;
        }
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

