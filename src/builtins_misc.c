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
extern int last_status;
#include <stdint.h>
#include <sys/resource.h>
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

