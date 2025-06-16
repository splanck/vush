/*
 * Test and conditional builtin commands
 *
 * This file implements the POSIX test/[ builtin and the [[ conditional
 * expression evaluator.
 */
#define _GNU_SOURCE
#include "builtins.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fnmatch.h>

extern int last_status;

static int eval_primary(char ***cur);
static int eval_not(char ***cur);
static int eval_and(char ***cur);
static int eval_or(char ***cur);

/* Helper evaluating a single primary expression and advancing cur. */
static int eval_primary(char ***cur) {
    char **c = *cur;
    int n = 0;
    while (c[n] && strcmp(c[n], "-a") && strcmp(c[n], "-o") && n < 3)
        n++;
    if (n == 0)
        return 1;
    char *a0 = c[0];
    char *a1 = n > 1 ? c[1] : NULL;
    char *a2 = n > 2 ? c[2] : NULL;
    c += n;
    *cur = c;
    int r = 1;
    if (n == 1) {
        r = a0[0] ? 0 : 1;
    } else if (n == 2) {
        if (strcmp(a0, "-n") == 0)
            r = a1[0] ? 0 : 1;
        else if (strcmp(a0, "-z") == 0)
            r = a1[0] ? 1 : 0;
        else if (strcmp(a0, "-e") == 0) {
            struct stat st;
            r = stat(a1, &st) == 0 ? 0 : 1;
        } else if (strcmp(a0, "-f") == 0) {
            struct stat st;
            r = stat(a1, &st) == 0 && S_ISREG(st.st_mode) ? 0 : 1;
        } else if (strcmp(a0, "-d") == 0) {
            struct stat st;
            r = stat(a1, &st) == 0 && S_ISDIR(st.st_mode) ? 0 : 1;
        } else if (strcmp(a0, "-b") == 0) {
            struct stat st;
            r = stat(a1, &st) == 0 && S_ISBLK(st.st_mode) ? 0 : 1;
        } else if (strcmp(a0, "-c") == 0) {
            struct stat st;
            r = stat(a1, &st) == 0 && S_ISCHR(st.st_mode) ? 0 : 1;
        } else if (strcmp(a0, "-p") == 0) {
            struct stat st;
            r = stat(a1, &st) == 0 && S_ISFIFO(st.st_mode) ? 0 : 1;
        } else if (strcmp(a0, "-S") == 0) {
            struct stat st;
            r = stat(a1, &st) == 0 && S_ISSOCK(st.st_mode) ? 0 : 1;
        } else if (strcmp(a0, "-h") == 0 || strcmp(a0, "-L") == 0) {
            struct stat st;
            r = lstat(a1, &st) == 0 && S_ISLNK(st.st_mode) ? 0 : 1;
        } else if (strcmp(a0, "-s") == 0) {
            struct stat st;
            r = stat(a1, &st) == 0 && st.st_size > 0 ? 0 : 1;
        } else if (strcmp(a0, "-g") == 0) {
            struct stat st;
            r = stat(a1, &st) == 0 && (st.st_mode & S_ISGID) ? 0 : 1;
        } else if (strcmp(a0, "-u") == 0) {
            struct stat st;
            r = stat(a1, &st) == 0 && (st.st_mode & S_ISUID) ? 0 : 1;
        } else if (strcmp(a0, "-k") == 0) {
            struct stat st;
            r = stat(a1, &st) == 0 && (st.st_mode & S_ISVTX) ? 0 : 1;
        } else if (strcmp(a0, "-O") == 0) {
            struct stat st;
            r = stat(a1, &st) == 0 && st.st_uid == geteuid() ? 0 : 1;
        } else if (strcmp(a0, "-G") == 0) {
            struct stat st;
            r = stat(a1, &st) == 0 && st.st_gid == getegid() ? 0 : 1;
        } else if (strcmp(a0, "-t") == 0) {
            int fd = atoi(a1);
            r = isatty(fd) ? 0 : 1;
        } else if (strcmp(a0, "-r") == 0) {
            r = access(a1, R_OK) == 0 ? 0 : 1;
        } else if (strcmp(a0, "-w") == 0) {
            r = access(a1, W_OK) == 0 ? 0 : 1;
        } else if (strcmp(a0, "-x") == 0) {
            r = access(a1, X_OK) == 0 ? 0 : 1;
        }
    } else if (n == 3) {
        if (strcmp(a1, "=") == 0)
            r = strcmp(a0, a2) == 0 ? 0 : 1;
        else if (strcmp(a1, "!=") == 0)
            r = strcmp(a0, a2) != 0 ? 0 : 1;
        else if (strcmp(a1, "-eq") == 0)
            r = (atoi(a0) == atoi(a2)) ? 0 : 1;
        else if (strcmp(a1, "-ne") == 0)
            r = (atoi(a0) != atoi(a2)) ? 0 : 1;
        else if (strcmp(a1, "-gt") == 0)
            r = (atoi(a0) > atoi(a2)) ? 0 : 1;
        else if (strcmp(a1, "-lt") == 0)
            r = (atoi(a0) < atoi(a2)) ? 0 : 1;
        else if (strcmp(a1, "-ge") == 0)
            r = (atoi(a0) >= atoi(a2)) ? 0 : 1;
        else if (strcmp(a1, "-le") == 0)
            r = (atoi(a0) <= atoi(a2)) ? 0 : 1;
        else if (strcmp(a1, "-nt") == 0) {
            struct stat st1, st2;
            if (stat(a0, &st1) == 0 && stat(a2, &st2) == 0)
                r = (st1.st_mtime > st2.st_mtime) ? 0 : 1;
            else
                r = 1;
        } else if (strcmp(a1, "-ot") == 0) {
            struct stat st1, st2;
            if (stat(a0, &st1) == 0 && stat(a2, &st2) == 0)
                r = (st1.st_mtime < st2.st_mtime) ? 0 : 1;
            else
                r = 1;
        } else if (strcmp(a1, "-ef") == 0) {
            struct stat st1, st2;
            if (stat(a0, &st1) == 0 && stat(a2, &st2) == 0)
                r = (st1.st_dev == st2.st_dev && st1.st_ino == st2.st_ino) ? 0 : 1;
            else
                r = 1;
        }
    }
    return r;
}

/* Recursive descent for !, -a and -o operators. */
static int eval_not(char ***cur) {
    char **c = *cur;
    if (c[0] && strcmp(c[0], "!") == 0) {
        c++;
        *cur = c;
        int r = eval_not(cur);
        return r ? 0 : 1;
    }
    return eval_primary(cur);
}

static int eval_and(char ***cur) {
    int r = eval_not(cur);
    while ((*cur)[0] && strcmp((*cur)[0], "-a") == 0) {
        (*cur)++;
        int r2 = eval_not(cur);
        r = (r == 0 && r2 == 0) ? 0 : 1;
    }
    return r;
}

static int eval_or(char ***cur) {
    int r = eval_and(cur);
    while ((*cur)[0] && strcmp((*cur)[0], "-o") == 0) {
        (*cur)++;
        int r2 = eval_and(cur);
        r = (r == 0 || r2 == 0) ? 0 : 1;
    }
    return r;
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
    char **cur = args + 1;
    count--;

    int res = eval_or(&cur);
    last_status = res;
    return 1;
}

/* [[ ... ]] conditional expression evaluator with pattern matching. */
int builtin_cond(char **args) {
    int count = 0;
    while (args[count]) count++;
    char **av = args;
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
    return res;
}
