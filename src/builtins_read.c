/*
 * vush - a simple UNIX shell
 * Licensed under the BSD 2-Clause Simplified License.
 * Implementation of the read builtin.
 */

#define _GNU_SOURCE
#include "builtins.h"
#include "parser.h"
#include "vars.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "shell_state.h"
#include "strarray.h"
#include <unistd.h>
#include <errno.h>
#include <termios.h>
#include <sys/select.h>
#include "util.h"

/* ---- helper functions for builtin_read -------------------------------- */
static int parse_read_options(char **args, int *raw, const char **array_name,
                              const char **prompt, int *nchars, int *silent,
                              int *timeout, int *fd, int *idx) {
    int i = 1;
    *raw = 0;
    *array_name = NULL;
    *prompt = NULL;
    *nchars = -1;
    *silent = 0;
    *timeout = -1;
    *fd = STDIN_FILENO;

    for (; args[i] && args[i][0] == '-'; i++) {
        if (strcmp(args[i], "-r") == 0) {
            *raw = 1;
        } else if (strcmp(args[i], "-a") == 0 && args[i + 1]) {
            *array_name = args[i + 1];
            i++;
        } else if (strcmp(args[i], "-p") == 0 && args[i + 1]) {
            *prompt = args[i + 1];
            i++;
        } else if (strcmp(args[i], "-n") == 0 && args[i + 1]) {
            if (parse_positive_int(args[i + 1], nchars) < 0)
                return -1;
            i++;
        } else if (strcmp(args[i], "-s") == 0) {
            *silent = 1;
        } else if (strcmp(args[i], "-t") == 0 && args[i + 1]) {
            if (parse_positive_int(args[i + 1], timeout) < 0)
                return -1;
            i++;
        } else if (strcmp(args[i], "-u") == 0 && args[i + 1]) {
            if (parse_positive_int(args[i + 1], fd) < 0)
                return -1;
            i++;
        } else {
            break;
        }
    }

    *idx = i;
    return 0;
}

static char **split_array_values(char *line, int *count, char sep) {
    StrArray arr;
    strarray_init(&arr);
    *count = 0;
    char *p = line;
    while (*p) {
        while (*p == sep)
            p++;
        if (*p == '\0')
            break;
        char *start = p;
        while (*p && *p != sep)
            p++;
        if (*p)
            *p++ = '\0';
        char *dup = strdup(start);
        if (!dup || strarray_push(&arr, dup) == -1) {
            free(dup);
            strarray_release(&arr);
            *count = 0;
            return NULL;
        }
    }
    char **vals = strarray_finish(&arr);
    if (!vals)
        return NULL;
    *count = arr.count ? arr.count - 1 : 0;
    if (*count == 0)
        vals[0] = NULL;
    return vals;
}

static void assign_read_vars(char **args, int idx, char *line, char sep) {
    int var_count = 0;
    for (int i = idx; args[i]; i++)
        var_count++;

    char *p = line;
    for (int i = 0; i < var_count; i++) {
        while (*p == sep)
            p++;
        char *val = p;
        if (i < var_count - 1) {
            while (*p && *p != sep)
                p++;
            if (*p)
                *p++ = '\0';
        }
        set_shell_var(args[idx + i], val);
    }
}

static int read_fd_line(int fd, char *buf, size_t size, int nchars,
                        int timeout, int silent) {
    struct termios orig;
    int use_tty = silent && isatty(fd);
    if (use_tty) {
        if (tcgetattr(fd, &orig) == -1)
            return -1;
        struct termios raw = orig;
        raw.c_lflag &= ~(ECHO);
        if (tcsetattr(fd, TCSAFLUSH, &raw) == -1)
            return -1;
    }

    size_t pos = 0;
    while (pos < size - 1) {
        if (timeout >= 0) {
            if (fd >= FD_SETSIZE) {
                if (use_tty)
                    tcsetattr(fd, TCSANOW, &orig);
                errno = EINVAL;
                return -1;
            }
            fd_set set;
            FD_ZERO(&set);
            FD_SET(fd, &set);
            struct timeval tv = { timeout, 0 };
            int rv;
            do {
                rv = select(fd + 1, &set, NULL, NULL, &tv);
            } while (rv == -1 && errno == EINTR);
            if (rv == 0) {
                if (use_tty)
                    tcsetattr(fd, TCSANOW, &orig);
                return 2; /* timeout */
            }
            if (rv < 0) {
                if (use_tty)
                    tcsetattr(fd, TCSANOW, &orig);
                return -1;
            }
        }

        char c;
        ssize_t n;
        do {
            n = read(fd, &c, 1);
        } while (n == -1 && errno == EINTR);
        if (n == 0 || (n > 0 && pos == 0 && c == 0x04)) {
            if (use_tty)
                tcsetattr(fd, TCSANOW, &orig);
            errno = 0;
            return 1; /* EOF */
        }
        if (n < 0) {
            if (use_tty)
                tcsetattr(fd, TCSANOW, &orig);
            return -1;
        }
        if (c == '\n' || c == '\r')
            break;
        buf[pos++] = c;
        if (nchars >= 0 && (int)pos >= nchars)
            break;
    }
    buf[pos] = '\0';
    if (use_tty) {
        tcsetattr(fd, TCSANOW, &orig);
        putchar('\n');
        fflush(stdout);
    }
    return 0;
}


/* Read a line from input and assign words to variables. */
int builtin_read(char **args) {
    int raw = 0;
    int silent = 0;
    int nchars = -1;
    int timeout = -1;
    int fd = STDIN_FILENO;
    const char *array_name = NULL;
    const char *prompt = NULL;
    int idx;
    if (parse_read_options(args, &raw, &array_name, &prompt, &nchars,
                           &silent, &timeout, &fd, &idx) != 0) {
        fprintf(stderr,
                "usage: read [-r] [-a NAME] [-p prompt] [-n nchars] [-s] [-t timeout] [-u fd] [NAME...]\n");
        last_status = 1;
        return 1;
    }

    if (prompt) {
        fputs(prompt, stdout);
        fflush(stdout);
    }

    char line[MAX_LINE];
    int r = read_fd_line(fd, line, sizeof(line), nchars, timeout, silent);
    if (r != 0) {
        last_status = 1;
        return 1;
    }

    if (!raw) {
        char *src = line;
        char *dst = line;
        while (*src) {
            if (*src == '\\' && src[1]) {
                src++;
                *dst++ = *src++;
            } else {
                *dst++ = *src++;
            }
        }
        *dst = '\0';
    }

    const char *ifs = get_shell_var("IFS");
    if (!ifs)
        ifs = getenv("IFS");
    char sep = (ifs && *ifs) ? ifs[0] : ' ';

    int array_mode = array_name != NULL;
    if (array_mode) {
        int count = 0;
        char **vals = split_array_values(line, &count, sep);
        if (vals) {
            set_shell_array(array_name, vals, count);
            for (int i = 0; i < count; i++)
                free(vals[i]);
        }
        /* free(NULL) is safe; handles split_array_values() failure gracefully */
        free(vals);
    } else {
        int var_count = 0;
        for (int i = idx; args[i]; i++)
            var_count++;
        if (var_count == 0) {
            set_shell_var("REPLY", line);
        } else {
            assign_read_vars(args, idx, line, sep);
        }
    }
    last_status = 0;
    return 1;
}

