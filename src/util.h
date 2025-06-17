/*
 * Miscellaneous utility helpers used across the shell.
 * Functions declared here provide I/O conveniences such as
 * reading logical lines and opening files for redirection.
 */
#ifndef VUSH_UTIL_H
#define VUSH_UTIL_H
#include <stdio.h>
/* Reads a logical line from FILE, merging backslash continuations.
 * Returns the buffer on success or NULL on EOF or error. */
char *read_logical_line(FILE *f, char *buf, size_t size);
/* Open PATH for output redirection.
 * APPEND non-zero opens in append mode.
 * FORCE overrides the noclobber option when set.
 * Returns a file descriptor or -1 on failure. */
int open_redirect(const char *path, int append, int force);
/* Construct a path using ENV_VAR if set, otherwise SECONDARY if set,
 * falling back to "$HOME/DEFAULT_NAME". NULL is returned when HOME is not
 * set. The returned string must be freed by the caller. */
char *make_user_path(const char *env_var, const char *secondary,
                     const char *default_name);
/* Parse S as a non-negative integer.  Return 0 on success, -1 on error or
 * overflow.  The result is stored in OUT when successful. */
int parse_positive_int(const char *s, int *out);
/* Allocate memory with calloc and exit on failure */
void *xcalloc(size_t nmemb, size_t size);
#endif /* VUSH_UTIL_H */
