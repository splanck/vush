/*
 * vush - a simple UNIX shell
 * Licensed under the BSD 2-Clause Simplified License.
 * History reference expansion.
 */

/*
 * History expansion helper.
 *
 * ``expand_history`` examines a line about to be executed and replaces leading
 * ``!`` references with the appropriate command from history.  The supported
 * forms mirror those of many traditional shells:
 *   - ``!!`` expands to the previous command.
 *   - ``!N`` expands to entry ``N``.
 *   - ``!-N`` refers to the Nth previous command.
 *   - ``!string`` looks up the most recent command beginning with ``string``.
 *   - ``!$`` becomes the last word of the previous command and ``!*`` expands
 *     to all of its arguments.
 * On success a newly allocated string containing the expanded line is returned
 * and should be freed by the caller.  On error ``NULL`` is returned and
 * ``last_status`` is set.
 */
#define _GNU_SOURCE
#include "shell_state.h"
#include "history_expand.h"
#include "history.h"
#include "parser.h" /* for MAX_LINE */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "util.h"

/*
 * Expand leading history references in ``line`` and return the new string.  If
 * no expansion is performed a duplicate of ``line`` is returned.
 */
char *expand_history(const char *line) {
    const char *p = line;
    while (*p == ' ' || *p == '\t')
        p++;
    if (*p != '!')
        return xstrdup(line);
    if (p[1] == '\0' || isspace((unsigned char)p[1]))
        return xstrdup(line);
    const char *bang = p;
    const char *rest;
    char *expansion = NULL;
    char pref[MAX_LINE];
    if (p[1] == '!' && (p[2] == '\0' || isspace((unsigned char)p[2]))) {
        const char *tmp = history_last();
        if (tmp)
            expansion = xstrdup(tmp);
        rest = p + 2;
        if (!expansion) {
            fprintf(stderr, "history: event not found\n");
            last_status = 1;
            return NULL;
        }
    } else if (isdigit((unsigned char)p[1]) ||
               (p[1] == '-' && isdigit((unsigned char)p[2]))) {
        int neg = (p[1] == '-');
        p += neg ? 2 : 1;
        int n = 0;
        while (isdigit((unsigned char)*p) && n < MAX_LINE - 1)
            pref[n++] = *p++;
        pref[n] = '\0';
        rest = p;
        int id = atoi(pref);
        const char *tmp = neg ? history_get_relative(id) : history_get_by_id(id);
        if (tmp)
            expansion = xstrdup(tmp);
        if (!expansion) {
            fprintf(stderr, "history: event not found: %s%s\n", neg ? "-" : "", pref);
            last_status = 1;
            return NULL;
        }
    } else if (p[1] == '$' && (p[2] == '\0' || isspace((unsigned char)p[2]))) {
        expansion = history_last_word();
        rest = p + 2;
        if (!expansion) {
            fprintf(stderr, "history: event not found: $\n");
            last_status = 1;
            return NULL;
        }
    } else if (p[1] == '*' && (p[2] == '\0' || isspace((unsigned char)p[2]))) {
        expansion = history_all_words();
        rest = p + 2;
        if (!expansion) {
            fprintf(stderr, "history: event not found: *\n");
            last_status = 1;
            return NULL;
        }
    } else {
        int n = 0;
        p++;
        while (*p && !isspace((unsigned char)*p) && n < MAX_LINE - 1)
            pref[n++] = *p++;
        pref[n] = '\0';
        const char *tmp = history_find_prefix(pref);
        if (tmp)
            expansion = xstrdup(tmp);
        rest = p;
        if (!expansion) {
            fprintf(stderr, "history: event not found: %s\n", pref);
            last_status = 1;
            return NULL;
        }
    }
    size_t pre_len = (size_t)(bang - line);
    size_t exp_len = strlen(expansion);
    size_t rest_len = strlen(rest);
    char *res = xmalloc(pre_len + exp_len + rest_len + 1);
    memcpy(res, line, pre_len);
    memcpy(res + pre_len, expansion, exp_len);
    memcpy(res + pre_len + exp_len, rest, rest_len + 1);
    free(expansion);
    return res;
}
