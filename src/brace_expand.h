/*
 * vush - a simple UNIX shell
 * Licensed under the BSD 2-Clause Simplified License.
 * Brace expansion functions.
 */

#ifndef BRACE_EXPAND_H
#define BRACE_EXPAND_H

char **expand_braces(const char *word, int *count_out);

#endif /* BRACE_EXPAND_H */
