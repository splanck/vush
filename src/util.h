#ifndef VUSH_UTIL_H
#define VUSH_UTIL_H
#include <stdio.h>
char *read_logical_line(FILE *f, char *buf, size_t size);
int open_redirect(const char *path, int append);
#endif /* VUSH_UTIL_H */
