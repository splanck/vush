/*
 * Utility helpers for reading lines and printing messages.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <fcntl.h>
#include <unistd.h>
#include "options.h"
#include "parser.h" /* for MAX_LINE */
#include "util.h"

void *xcalloc(size_t nmemb, size_t size) {
    void *ptr = calloc(nmemb, size);
    if (!ptr) {
        perror("calloc");
        exit(1);
    }
    return ptr;
}

void *xmalloc(size_t size) {
    void *ptr = malloc(size);
    if (!ptr) {
        perror("malloc");
        exit(1);
    }
    return ptr;
}

char *xstrdup(const char *s) {
    char *ptr = strdup(s);
    if (!ptr) {
        perror("strdup");
        exit(1);
    }
    return ptr;
}
/*
 * Read a line continuing backslash escapes across multiple physical lines.
 */

static int read_physical_line(FILE *f, char *buf, size_t size) {
    int c;
    size_t len = 0;
    while ((c = fgetc(f)) != EOF) {
        if (c == '\n' || c == '\r') {
            if (c == '\r') {
                int next = fgetc(f);
                if (next != '\n' && next != EOF)
                    ungetc(next, f);
            }
            break;
        }
        if (len < size - 1)
            buf[len++] = (char)c;
    }
    if (c == EOF && len == 0)
        return 0;
    if (len >= size - 1) {
        buf[len] = '\0';
        while (c != EOF && c != '\n' && c != '\r')
            c = fgetc(f);
        if (c == '\r') {
            int next = fgetc(f);
            if (next != '\n' && next != EOF)
                ungetc(next, f);
        }
    }
    buf[len] = '\0';
    return 1;
}

char *read_logical_line(FILE *f, char *buf, size_t size) {
    if (!read_physical_line(f, buf, size))
        return NULL;
    size_t len = strlen(buf);
    while (len > 0 && buf[len - 1] == '\\') {
        do {
            buf[--len] = '\0';
        } while (len > 0 && buf[len - 1] == '\\');
        char cont[MAX_LINE];
        if (!read_physical_line(f, cont, sizeof(cont))) {
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

/*
 * Parse a non-negative integer from the given string.  Returns 0 on
 * success and -1 on failure or overflow.  The parsed value is stored in
 * *out when successful.
 */
int parse_positive_int(const char *s, int *out) {
    if (!s || !*s)
        return -1;

    char *end;
    errno = 0;
    long val = strtol(s, &end, 10);
    if (*end != '\0' || errno != 0 || val < 0 || val > INT_MAX)
        return -1;
    if (out)
        *out = (int)val;
    return 0;
}
