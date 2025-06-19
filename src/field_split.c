#define _GNU_SOURCE
#include "var_expand.h"
#include "vars.h"
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
    char **out = NULL;
    int count = 0;
    int last_nonspace = 0;

    while (*p) {
        unsigned char c = (unsigned char)*p;
        if (ws_tab[c]) {
            if (p > field_start) {
                char save = *p;
                *p = '\0';
                char **tmp = realloc(out, sizeof(char *) * (count + 1));
                if (!tmp) {
                    goto fail;
                }
                out = tmp;
                out[count] = strdup(field_start);
                if (!out[count]) {
                    goto fail;
                }
                count++;
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
            char **tmp = realloc(out, sizeof(char *) * (count + 1));
            if (!tmp) {
                goto fail;
            }
            out = tmp;
            out[count] = strdup(field_start);
            if (!out[count]) {
                goto fail;
            }
            count++;
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
        char **tmp = realloc(out, sizeof(char *) * (count + 1));
        if (!tmp) {
            goto fail;
        }
        out = tmp;
        out[count] = strdup(field_start);
        if (!out[count]) {
            goto fail;
        }
        count++;
    }

    free(dup);

    if (count == 0) {
        out = malloc(sizeof(char *));
        if (!out)
            return NULL;
        out[0] = NULL;
    } else {
        char **tmp = realloc(out, sizeof(char *) * (count + 1));
        if (!tmp) {
            goto fail_alloc;
        }
        out = tmp;
        out[count] = NULL;
    }
    if (count_out)
        *count_out = count;
    return out;

fail:
    free(dup);
fail_alloc:
    if (out) {
        for (int i = 0; i < count; i++)
            free(out[i]);
        free(out);
    }
    if (count_out)
        *count_out = 0;
    return NULL;
}
