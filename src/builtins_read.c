#define _GNU_SOURCE
#include "builtins.h"
#include "parser.h"
#include "vars.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern int last_status;

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

    if (!args[i])
        return -1;

    *idx = i;
    return 0;
}

static char **split_array_values(char *line, int *count) {
    char **vals = NULL;
    *count = 0;
    char *p = line;
    while (*p) {
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '\0')
            break;
        char *start = p;
        while (*p && *p != ' ' && *p != '\t')
            p++;
        if (*p)
            *p++ = '\0';
        vals = realloc(vals, sizeof(char *) * (*count + 1));
        if (!vals)
            return NULL;
        vals[*count] = strdup(start);
        (*count)++;
    }
    return vals;
}

static void assign_read_vars(char **args, int idx, char *line) {
    int var_count = 0;
    for (int i = idx; args[i]; i++)
        var_count++;

    char *p = line;
    for (int i = 0; i < var_count; i++) {
        while (*p == ' ' || *p == '\t')
            p++;
        char *val = p;
        if (i < var_count - 1) {
            while (*p && *p != ' ' && *p != '\t')
                p++;
            if (*p)
                *p++ = '\0';
        }
        set_shell_var(args[idx + i], val);
    }
}

int builtin_read(char **args) {
    int raw = 0;
    const char *array_name = NULL;
    int idx;
    if (parse_read_options(args, &raw, &array_name, &idx) != 0) {
        fprintf(stderr, "usage: read [-r] [-a NAME] NAME...\n");
        last_status = 1;
        return 1;
    }

    char line[MAX_LINE];
    if (!fgets(line, sizeof(line), stdin)) {
        last_status = 1;
        return 1;
    }

    size_t len = strlen(line);
    if (len && line[len - 1] == '\n')
        line[--len] = '\0';

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

    int array_mode = array_name != NULL;
    if (array_mode) {
        int count = 0;
        char **vals = split_array_values(line, &count);
        if (!vals)
            count = 0;
        set_shell_array(array_name, vals, count);
        for (int i = 0; i < count; i++)
            free(vals[i]);
        free(vals);
    } else {
        assign_read_vars(args, idx, line);
    }
    last_status = 0;
    return 1;
}

