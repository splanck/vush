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
    if (len && buf[len - 1] == '\r')
        buf[--len] = '\0';
    while (len > 0 && buf[len - 1] == '\\') {
        do {
            buf[--len] = '\0';
        } while (len > 0 && buf[len - 1] == '\\');
        char cont[MAX_LINE];
        if (!fgets(cont, sizeof(cont), f)) {
            /* restore removed backslashes if continuation fails */
            while (buf[len] == '\0' && len < size - 1) {
                buf[len++] = '\\';
            }
            buf[len] = '\0';
            break;
        }
        size_t nlen = strlen(cont);
        if (nlen && cont[nlen - 1] == '\n')
            cont[--nlen] = '\0';
        if (nlen && cont[nlen - 1] == '\r')
            cont[--nlen] = '\0';
        if (len + nlen < size) {
            memcpy(buf + len, cont, nlen + 1);
            len += nlen;
        } else {
            memcpy(buf + len, cont, size - len - 1);
            buf[size - 1] = '\0';
            len = strlen(buf);
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

int usage_error(const char *msg) {
    fprintf(stderr, "usage: %s\n", msg);
    return 1;
}
