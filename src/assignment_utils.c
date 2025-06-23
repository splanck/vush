#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "assignment_utils.h"
#include "vars.h"
#include "util.h"
#include "strarray.h"
#include "shell_state.h"

char **parse_array_values(const char *val, int *count) {
    *count = 0;
    char *body = strndup(val + 1, strlen(val) - 2);
    if (!body)
        return NULL;

    StrArray arr;
    strarray_init(&arr);
    char *p = body;
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

        char *dup = strdup(start);
        if (!dup || strarray_push(&arr, dup) == -1) {
            free(dup);
            free(body);
            strarray_release(&arr);
            *count = 0;
            return NULL;
        }
    }
    free(body);

    char **vals = strarray_finish(&arr);
    if (!vals)
        return NULL;
    *count = arr.count ? arr.count - 1 : 0;
    if (*count == 0) {
        vals[0] = NULL;
    }
    return vals;
}

void apply_array_assignment(const char *name, const char *val, int export_env) {
    int count = 0;
    char **vals = parse_array_values(val, &count);
    if (!vals && count > 0)
        return;

    set_shell_array(name, vals, count);

    if (export_env) {
        size_t joinlen = 0;
        for (int j = 0; j < count; j++)
            joinlen += strlen(vals[j]) + 1;
        char *joined = malloc(joinlen + 1);
        if (!joined) {
            /* Report allocation failure but still continue after setting
             * last_status to indicate the error. */
            perror("malloc");
            last_status = 1;
        } else {
            joined[0] = '\0';
            for (int j = 0; j < count; j++) {
                strcat(joined, vals[j]);
                if (j < count - 1)
                    strcat(joined, " ");
            }
            setenv(name, joined, 1);
            free(joined);
        }
    }

    for (int j = 0; j < count; j++)
        free(vals[j]);
    free(vals);
}

struct assign_backup *backup_assignments(PipelineSegment *pipeline) {
    if (pipeline->assign_count == 0)
        return NULL;

    struct assign_backup *backs = xcalloc(pipeline->assign_count, sizeof(*backs));

    for (int i = 0; i < pipeline->assign_count; i++) {
        char *eq = strchr(pipeline->assigns[i], '=');
        if (!eq) {
            backs[i].name = NULL;
            continue;
        }
        backs[i].name = strndup(pipeline->assigns[i], eq - pipeline->assigns[i]);
        if (!backs[i].name) {
            backs[i].env = backs[i].var = NULL;
            backs[i].had_env = backs[i].had_var = 0;
            continue;
        }
        const char *oe = getenv(backs[i].name);
        backs[i].had_env = oe != NULL;
        backs[i].env = oe ? strdup(oe) : NULL;
        if (oe && !backs[i].env) {
            free(backs[i].name);
            backs[i].name = NULL;
            continue;
        }
        const char *ov = get_shell_var(backs[i].name);
        backs[i].had_var = ov != NULL;
        backs[i].var = ov ? strdup(ov) : NULL;
        if (ov && !backs[i].var) {
            free(backs[i].env);
            free(backs[i].name);
            backs[i].name = NULL;
            continue;
        }
    }

    return backs;
}

void restore_assignments(PipelineSegment *pipeline, struct assign_backup *backs) {
    if (!backs)
        return;
    for (int i = 0; i < pipeline->assign_count; i++) {
        if (!backs[i].name)
            continue;
        if (backs[i].had_env)
            setenv(backs[i].name, backs[i].env, 1);
        else
            unsetenv(backs[i].name);
        if (backs[i].had_var)
            set_shell_var(backs[i].name, backs[i].var);
        else
            unset_shell_var(backs[i].name);
        free(backs[i].name);
        free(backs[i].env);
        free(backs[i].var);
    }
    free(backs);
}

