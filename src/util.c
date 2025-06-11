/*
 * Utility helpers for reading lines and printing messages.
 */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "options.h"
#include "parser.h" /* for MAX_LINE */
#include "util.h"
/*
 * Read a line continuing backslash escapes across multiple physical lines.
 */

char *read_logical_line(FILE *f, char *buf, size_t size) {
    if (!fgets(buf, size, f))
        return NULL;
    size_t len = strlen(buf);
    if (len && buf[len - 1] == '\n')
        buf[--len] = '\0';
    while (len > 0) {
        size_t bs = 0;
        while (bs < len && buf[len - 1 - bs] == '\\')
            bs++;
        if (bs % 2 == 1) {
            buf[--len] = '\0';
            char cont[MAX_LINE];
            if (!fgets(cont, sizeof(cont), f))
                break;
            size_t nlen = strlen(cont);
            if (nlen && cont[nlen - 1] == '\n')
                cont[--nlen] = '\0';
            if (len + nlen < size) {
                memcpy(buf + len, cont, nlen + 1);
                len += nlen;
            } else {
                memcpy(buf + len, cont, size - len - 1);
                buf[size - 1] = '\0';
                len = strlen(buf);
            }
        } else {
            break;
        }
    }
    return buf;
}

int open_redirect(const char *path, int append, int force) {
    int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
    if (opt_noclobber && !append && !force)
        flags |= O_EXCL;
    return open(path, flags, 0644);
}
