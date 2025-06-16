/*
 * Utility helpers for reading lines and printing messages.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
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

char *make_user_path(const char *env_var, const char *secondary,
                     const char *default_name) {
    if (env_var) {
        const char *val = getenv(env_var);
        if (val && *val)
            return strdup(val);
    }
    if (secondary) {
        const char *val = getenv(secondary);
        if (val && *val)
            return strdup(val);
    }
    const char *home = getenv("HOME");
    if (!home || !*home)
        return NULL;
    size_t len = strlen(home) + 1 + strlen(default_name) + 1;
    char *res = malloc(len);
    if (!res)
        return NULL;
    snprintf(res, len, "%s/%s", home, default_name);
    return res;
}
