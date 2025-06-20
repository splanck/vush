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

/* ---- helper functions for builtin_read -------------------------------- */
static int parse_read_options(char **args, int *raw, const char **array_name,
                              int *idx) {
    int i = 1;
    *raw = 0;
    *array_name = NULL;

    if (args[i] && strcmp(args[i], "-r") == 0) {
        *raw = 1;
        i++;
    }

    if (args[i] && strcmp(args[i], "-a") == 0 && args[i + 1]) {
        *array_name = args[i + 1];
        i += 2;
    }

    /* Do not error when no variable names remain */
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

static int read_terminal_line(char *buf, size_t size) {
    size_t pos = 0;
    while (pos < size - 1) {
        char c;
        ssize_t n;
        do {
            n = read(STDIN_FILENO, &c, 1);
        } while (n == -1 && errno == EINTR);
        if (n == 0 || (n > 0 && pos == 0 && c == 0x04)) {
            errno = 0;
            last_status = 1;
            return 1; /* EOF */
        }
        if (n < 0)
            return -1;
        if (c == '\n' || c == '\r')
            break;
        buf[pos++] = c;
    }
    buf[pos] = '\0';
    return 0;
}

int builtin_read(char **args) {
    int raw = 0;
    const char *array_name = NULL;
    int idx;
    if (parse_read_options(args, &raw, &array_name, &idx) != 0) {
        fprintf(stderr, "usage: read [-r] [-a NAME] [NAME...]\n");
        last_status = 1;
        return 1;
    }

    char line[MAX_LINE];
    int r = read_terminal_line(line, sizeof(line));
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

