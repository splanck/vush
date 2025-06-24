/*
 * vush - a simple UNIX shell
 * Licensed under the BSD 2-Clause Simplified License.
 * System setting builtins like ulimit.
 */

/*
 * System builtins
 *
 * This module implements builtins that modify process limits or file
 * creation masks such as `ulimit` and `umask`.
 */
#define _GNU_SOURCE
#include "shell_state.h"
#include "builtins.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/resource.h>


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

/* Display or set the process file mode creation mask. */
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
#ifdef RLIMIT_RSS
        {'m', RLIMIT_RSS},
#endif
        {'f', RLIMIT_FSIZE},
#ifdef RLIMIT_NPROC
        {'u', RLIMIT_NPROC},
#endif
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
                fprintf(stderr, "usage: ulimit [-HS] [-a|-c|-d|-f|-m|-n|-s|-t|-u|-v] [limit]\n");
                return 1;
            }
        } else {
            fprintf(stderr, "usage: ulimit [-HS] [-a|-c|-d|-f|-m|-n|-s|-t|-u|-v] [limit]\n");
            return 1;
        }
    }

    if (show_all) {
        if (args[i]) {
            fprintf(stderr, "usage: ulimit [-HS] [-a|-c|-d|-f|-m|-n|-s|-t|-u|-v] [limit]\n");
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
        fprintf(stderr, "usage: ulimit [-HS] [-a|-c|-d|-f|-m|-n|-s|-t|-u|-v] [limit]\n");
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
    if (hard) {
        rl.rlim_max = val;
        if (rl.rlim_cur > rl.rlim_max)
            rl.rlim_cur = rl.rlim_max;
    } else {
        rl.rlim_cur = val;
        if (rl.rlim_max < rl.rlim_cur)
            rl.rlim_max = rl.rlim_cur;
    }
    if (setrlimit(resource, &rl) != 0) {
        perror("ulimit");
        last_status = 1;
    } else {
        last_status = 0;
    }
    return 1;
}

