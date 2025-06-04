#define _GNU_SOURCE
#include "parser.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

char *expand_var(const char *token) {
    if (token[0] == '~') {
        const char *home = getenv("HOME");
        if (!home) home = "";
        size_t home_len = strlen(home);
        size_t token_len = strlen(token);
        char *tmp = malloc(home_len + token_len);
        if (!tmp) return NULL;
        strcpy(tmp, home);
        strcat(tmp, token + 1);
        char *ret = strdup(tmp);
        free(tmp);
        return ret;
    }
    if (token[0] != '$') return strdup(token);
    const char *val = getenv(token + 1);
    return strdup(val ? val : "");
}

int parse_line(char *line, char **args, int *background) {
    int argc = 0;
    *background = 0;
    char *p = line;
    while (*p && argc < MAX_TOKENS - 1) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;

        char buf[MAX_LINE];
        int len = 0;
        int do_expand = 1;

        if (*p == '\'') {
            /* single quoted token - copy verbatim */
            do_expand = 0;
            p++; /* skip opening quote */
            while (*p && *p != '\'' && len < MAX_LINE - 1) {
                buf[len++] = *p++;
            }
            if (*p == '\'') p++; /* skip closing quote */
        } else {
            int in_double = 0;
            if (*p == '"') {
                in_double = 1;
                p++; /* skip opening quote */
            }
            int first = 1;
            while (*p && (in_double || (*p != ' ' && *p != '\t'))) {
                if (*p == '\\') {
                    p++;
                    if (*p) {
                        if (len < MAX_LINE - 1) buf[len++] = *p;
                        if (first) do_expand = 0;
                        if (*p) p++;
                    }
                    first = 0;
                    continue;
                }
                if (in_double && *p == '"') {
                    p++; /* end quote */
                    break;
                }
                if (len < MAX_LINE - 1) buf[len++] = *p;
                p++;
                first = 0;
            }
        }

        buf[len] = '\0';
        args[argc++] = do_expand ? expand_var(buf) : strdup(buf);
    }

    if (argc > 0 && strcmp(args[argc-1], "&") == 0) {
        *background = 1;
        free(args[argc-1]);
        argc--;
    }

    args[argc] = NULL;
    return argc;
}

