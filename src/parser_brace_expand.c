/*
 * vush - a simple UNIX shell
 * Licensed under the BSD 2-Clause Simplified License.
 * Brace expansion support for parser tokens.
 */

#define _GNU_SOURCE
#include "parser_brace_expand.h"
#include "brace_expand.h"
#include <stdlib.h>

/* Expand TOK using brace expansion unless quoted or a parameter name. */
char **expand_token_braces(char *tok, int quoted, int *count) {
    if (!quoted && !(tok[0] == '$' && tok[1] == '{')) {
        char **btoks = expand_braces(tok, count);
        free(tok);
        return btoks;
    }

    char **btoks = malloc(2 * sizeof(char *));
    if (!btoks) {
        free(tok);
        return NULL;
    }
    btoks[0] = tok;
    btoks[1] = NULL;
    if (count) *count = 1;
    return btoks;
}
