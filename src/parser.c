/*
 * vush - a simple UNIX shell
 * Licensed under the GNU GPLv3.
 */

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

PipelineSegment *parse_line(char *line, int *background) {
    *background = 0;
    PipelineSegment *head = calloc(1, sizeof(PipelineSegment));
    if (!head) return NULL;
    PipelineSegment *cur = head;
    int argc = 0;
    char *p = line;
    while (*p && argc < MAX_TOKENS - 1) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '#') break; /* comment */

        if (*p == '|') {
            cur->argv[argc] = NULL;
            PipelineSegment *next = calloc(1, sizeof(PipelineSegment));
            cur->next = next;
            cur = next;
            argc = 0;
            p++;
            continue;
        }

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
        cur->argv[argc++] = do_expand ? expand_var(buf) : strdup(buf);
    }

    if (argc > 0 && strcmp(cur->argv[argc-1], "&") == 0) {
        *background = 1;
        free(cur->argv[argc-1]);
        cur->argv[argc-1] = NULL;
    } else {
        cur->argv[argc] = NULL;
    }

    return head;
}

void free_pipeline(PipelineSegment *p) {
    while (p) {
        PipelineSegment *next = p->next;
        for (int i = 0; p->argv[i]; i++)
            free(p->argv[i]);
        free(p);
        p = next;
    }
}

