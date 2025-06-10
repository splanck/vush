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
 * Returns a file descriptor or -1 on failure. */
int open_redirect(const char *path, int append);
#endif /* VUSH_UTIL_H */
