#define _GNU_SOURCE
#include "prompt_expand.h"
#include "var_expand.h"
#include "lexer.h"
#include "parser.h" /* for MAX_LINE */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Expand escape sequences and variables found in PROMPT using the normal
 * token expansion logic. A new string is returned. */
char *expand_prompt(const char *prompt) {
    if (!prompt)
        return strdup("");

    /* Wrap the prompt in double quotes so the normal token reader can
     * interpret backslash escapes and quoting rules. */
    size_t len = strlen(prompt);
    char *tmp = malloc(len + 3);
    if (!tmp)
        return strdup("");
    tmp[0] = '"';
    memcpy(tmp + 1, prompt, len);
    tmp[len + 1] = '"';
    tmp[len + 2] = '\0';

    char *p = tmp;
    int quoted = 0;
    int do_expand = 1;
    char *res = read_token(&p, &quoted, &do_expand);
    if (getenv("VUSH_DEBUG"))
        fprintf(stderr, "expand_prompt token='%s' de=%d\n", res ? res : "", do_expand);
    free(tmp);
    if (!res)
        return strdup("");

    /* When expansion is requested run the normal variable expansion logic.
     * This interprets variables and command substitutions while leaving any
     * trailing whitespace intact. */
    if (do_expand) {
        char *out = expand_var(res);
        if (!out) {
            free(res);
            return strdup("");
        }
        if (getenv("VUSH_DEBUG"))
            fprintf(stderr, "expand_prompt result='%s'\n", out);
        free(res);
        res = out;
    }

    return res;
}

