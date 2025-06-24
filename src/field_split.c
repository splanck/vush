/*
 * vush - a simple UNIX shell
 * Licensed under the BSD 2-Clause Simplified License.
 * Field splitting after expansions.
 */

#define _GNU_SOURCE
#include "var_expand.h"
#include "vars.h"
#include "strarray.h"
#include <stdlib.h>
#include <string.h>

char **split_fields(const char *text, int *count_out) {
    if (count_out)
        *count_out = 0;

    const char *ifs = get_shell_var("IFS");
    if (!ifs)
        ifs = getenv("IFS");
    if (!ifs)
        ifs = " \t\n";

    if (!*ifs) {
        char **res = malloc(2 * sizeof(char *));
        if (!res)
            return NULL;
        res[0] = strdup(text);
        if (!res[0]) {
            free(res);
            return NULL;
        }
        res[1] = NULL;
        if (count_out)
            *count_out = 1;
        return res;
    }

    char ws_tab[256] = {0};
    char other_tab[256] = {0};
    for (const char *p = ifs; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == ' ' || c == '\t' || c == '\n')
            ws_tab[c] = 1;
        else
            other_tab[c] = 1;
    }

    char *dup = strdup(text);
    if (!dup)
        return NULL;
    char *p = dup;
    char *field_start = dup;
    StrArray arr;
    strarray_init(&arr);
    int last_nonspace = 0;

    while (*p) {
        unsigned char c = (unsigned char)*p;
        if (ws_tab[c]) {
            if (p > field_start) {
                char save = *p;
                *p = '\0';
                char *dup_field = strdup(field_start);
                if (!dup_field || strarray_push(&arr, dup_field) == -1) {
                    free(dup_field);
                    goto fail;
                }
                *p = save;
            }
            while (ws_tab[(unsigned char)*p])
                p++;
            field_start = p;
            last_nonspace = 0;
            continue;
        } else if (other_tab[c]) {
            char save = *p;
            *p = '\0';
            char *dup_field = strdup(field_start);
            if (!dup_field || strarray_push(&arr, dup_field) == -1) {
                free(dup_field);
                goto fail;
            }
            *p = save;
            p++;
            field_start = p;
            last_nonspace = 1;
            continue;
        }
        last_nonspace = 0;
        p++;
    }

    if (p > field_start || last_nonspace) {
        char *dup_field = strdup(field_start);
        if (!dup_field || strarray_push(&arr, dup_field) == -1) {
            free(dup_field);
            goto fail;
        }
    }

    free(dup);

    int cnt = arr.count;
    char **res = strarray_finish(&arr);
    if (!res)
        goto fail_alloc;
    if (count_out)
        *count_out = cnt;
    return res;

fail:
    free(dup);
fail_alloc:
    strarray_release(&arr);
    if (count_out)
        *count_out = 0;
    return NULL;
}
