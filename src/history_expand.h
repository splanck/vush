/*
 * vush - a simple UNIX shell
 * Licensed under the BSD 2-Clause Simplified License.
 * History expansion utilities.
 */

#ifndef HISTORY_EXPAND_H
#define HISTORY_EXPAND_H

/*
 * Expand history references at the beginning of ``line`` (e.g. "!!" or
 * "!42") using the in-memory history list.  The returned string is newly
 * allocated and must be freed by the caller.
 */
char *expand_history(const char *line);

#endif /* HISTORY_EXPAND_H */
