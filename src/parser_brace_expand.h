/*
 * vush - a simple UNIX shell
 * Licensed under the BSD 2-Clause Simplified License.
 * Brace expansion parsing.
 */

#ifndef PARSER_BRACE_EXPAND_H
#define PARSER_BRACE_EXPAND_H

char **expand_token_braces(char *tok, int quoted, int *count);

#endif /* PARSER_BRACE_EXPAND_H */
