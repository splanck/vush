#define _GNU_SOURCE
#include "brace_expand.h"
#include "parser.h" /* for MAX_LINE and MAX_TOKENS */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* Expand simple brace patterns like {foo,bar} or {1..3}. Returns an array
 * of allocated strings terminated by NULL. The caller must free each string
 * and the array itself. If no expansion occurs the array contains a single
 * copy of WORD. Nested braces are not supported. */
char **expand_braces(const char *word, int *count_out) {
    if (count_out)
        *count_out = 0;

    const char *lb = strchr(word, '{');
    const char *rb = lb ? strchr(lb, '}') : NULL;
    if (!lb || !rb || rb < lb) {
        char **res = malloc(2 * sizeof(char *));
        if (!res)
            return NULL;
        res[0] = strdup(word);
        if (!res[0]) {
            free(res);
            return NULL;
        }
        res[1] = NULL;
        if (count_out)
            *count_out = 1;
        return res;
    }

    char prefix[MAX_LINE];
    size_t prelen = (size_t)(lb - word);
    if (prelen >= sizeof(prefix))
        prelen = sizeof(prefix) - 1;
    memcpy(prefix, word, prelen);
    prefix[prelen] = '\0';

    char inner[MAX_LINE];
    size_t inlen = (size_t)(rb - lb - 1);
    if (inlen >= sizeof(inner))
        inlen = sizeof(inner) - 1;
    memcpy(inner, lb + 1, inlen);
    inner[inlen] = '\0';

    char suffix[MAX_LINE];
    strncpy(suffix, rb + 1, sizeof(suffix));
    suffix[sizeof(suffix) - 1] = '\0';

    char **res = malloc(sizeof(char *) * MAX_TOKENS);
    if (!res)
        return NULL;
    int count = 0;

    char *dots = strstr(inner, "..");
    if (dots) {
        char left[32], right[32];
        size_t llen = (size_t)(dots - inner);
        if (llen >= sizeof(left))
            llen = sizeof(left) - 1;
        memcpy(left, inner, llen);
        left[llen] = '\0';
        strncpy(right, dots + 2, sizeof(right));
        right[sizeof(right) - 1] = '\0';
        char *ep1, *ep2;
        long start = strtol(left, &ep1, 10);
        long end = strtol(right, &ep2, 10);
        if (*ep1 == '\0' && *ep2 == '\0') {
            int step = start <= end ? 1 : -1;
            for (long n = start; (step > 0 ? n <= end : n >= end) && count < MAX_TOKENS - 1; n += step) {
                char num[32];
                snprintf(num, sizeof(num), "%ld", n);
                size_t len = strlen(prefix) + strlen(num) + strlen(suffix) + 1;
                char *tmp = malloc(len);
                if (!tmp) {
                    for (int i = 0; i < count; i++)
                        free(res[i]);
                    free(res);
                    return NULL;
                }
                snprintf(tmp, len, "%s%s%s", prefix, num, suffix);
                res[count++] = tmp;
            }
            res[count] = NULL;
            if (count_out)
                *count_out = count;
            return res;
        }
    }

    char *dup = strdup(inner);
    if (!dup) {
        free(res);
        return NULL;
    }
    char *sp = NULL;
    char *tok = strtok_r(dup, ",", &sp);
    while (tok && count < MAX_TOKENS - 1) {
        size_t len = strlen(prefix) + strlen(tok) + strlen(suffix) + 1;
        char *tmp = malloc(len);
        if (!tmp) {
            free(dup);
            for (int i = 0; i < count; i++)
                free(res[i]);
            free(res);
            return NULL;
        }
        snprintf(tmp, len, "%s%s%s", prefix, tok, suffix);
        res[count++] = tmp;
        tok = strtok_r(NULL, ",", &sp);
    }
    free(dup);
    if (count == 0) {
        res[count] = strdup(word);
        if (!res[count]) {
            for (int i = 0; i < count; i++)
                free(res[i]);
            free(res);
            return NULL;
        }
        count++;
    }
    res[count] = NULL;
    if (count_out)
        *count_out = count;
    return res;
}

