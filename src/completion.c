/*
 * Simple filename and command completion utilities.
 */
#define _GNU_SOURCE
#include "completion.h"
#include "builtins.h"
#include "parser.h" /* for MAX_LINE */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "strarray.h"
#include <unistd.h>
#include <limits.h>
#include "shell_state.h"

static int cmpstr(const void *a, const void *b) {
    const char *aa = *(const char **)a;
    const char *bb = *(const char **)b;
    return strcmp(aa, bb);
}

/* Check if name already exists in matches[0..count-1]. */
static int has_match(char **matches, int count, const char *name) {
    for (int i = 0; i < count; i++) {
        if (strcmp(matches[i], name) == 0)
            return 1;
    }
    return 0;
}

/* Collect builtin command names matching the prefix.  The returned array
 * is NULL terminated and must be freed by the caller.  On allocation
 * failure NULL is returned and countp is set to 0. */
static char **collect_builtin_matches(const char *prefix, int prefix_len,
                                      int *countp) {
    StrArray arr; strarray_init(&arr);
    int count = 0; int cap = 16;
    arr.items = malloc(cap * sizeof(char *));
    if (!arr.items) {
        /* allocation failure when initializing match array */
        perror("malloc");
        last_status = 1;
        if (countp) *countp = 0;
        return NULL;
    }
    arr.capacity = cap;

    const char **bn = get_builtin_names();
    for (int i = 0; bn[i]; i++) {
        if (strcmp(bn[i], "exec") == 0)
            continue;
        if (strncmp(bn[i], prefix, prefix_len) == 0) {
            if (count == cap) {
                cap *= 2;
                char **tmp = realloc(arr.items, cap * sizeof(char *));
                if (!tmp) {
                    for (int j = 0; j < count; j++)
                        free(arr.items[j]);
                    free(arr.items);
                    if (countp) *countp = 0;
                    return NULL;
                }
                arr.items = tmp; arr.capacity = cap;
            }
            char *dup = strdup(bn[i]);
            if (!dup) {
                for (int j = 0; j < count; j++)
                    free(arr.items[j]);
                free(arr.items);
                if (countp) *countp = 0;
                return NULL;
            }
            arr.items[count++] = dup; arr.count = count;
        }
    }

    if (countp)
        *countp = count;
    if (count == cap) {
        char **tmp = realloc(arr.items, (cap + 1) * sizeof(char *));
        if (!tmp) {
            for (int j = 0; j < count; j++)
                free(arr.items[j]);
            free(arr.items);
            if (countp) *countp = 0;
            return NULL;
        }
        arr.items = tmp; arr.capacity = cap + 1;
    }
    arr.items[count] = NULL; arr.count = count + 1;
    return arr.items;
}

/* Collect filesystem and PATH matches for the given prefix. */
static char **collect_matches(const char *prefix, int prefix_len, int *countp) {
    StrArray arr; strarray_init(&arr);
    int count = 0;
    int cap = 16;
    arr.items = malloc(cap * sizeof(char *));
    if (!arr.items) {
        perror("malloc");
        last_status = 1;
        arr.items = NULL;
    }

    DIR *d = opendir(".");
    if (!arr.items) {
        if (d)
            closedir(d); /* early return when initial allocation failed */
        return NULL;
    }
    if (d) {
        struct dirent *de;
        while ((de = readdir(d))) {
            if (strncmp(de->d_name, prefix, prefix_len) == 0) {
                if (access(de->d_name, X_OK) != 0)
                    continue;
                if (has_match(arr.items, count, de->d_name))
                    continue;
                if (count == cap) {
                    cap *= 2;
                    char **tmp = realloc(arr.items, cap * sizeof(char *));
                    if (!tmp) {
                        for (int j = 0; j < count; j++)
                            free(arr.items[j]);
                        free(arr.items);
                        closedir(d);
                        return NULL;
                    }
                    arr.items = tmp; arr.capacity = cap;
                }
                char *dup = strdup(de->d_name);
                if (!dup) {
                    for (int j = 0; j < count; j++)
                        free(arr.items[j]);
                    free(arr.items);
                    closedir(d);
                    return NULL;
                }
                arr.items[count++] = dup; arr.count = count;
            }
        }
        closedir(d);
    }

    const char *path = getenv("PATH");
    if (path && arr.items) {
        char *pdup = strdup(path);
        if (!pdup) {
            for (int j = 0; j < count; j++)
                free(arr.items[j]);
            free(arr.items);
            return NULL;
        }
        char *saveptr = NULL;
        char *dir = strtok_r(pdup, ":", &saveptr);
        while (dir) {
            const char *d = *dir ? dir : ".";
            DIR *pd = opendir(d);
            int found = 0;
            if (pd) {
                struct dirent *pe;
                while ((pe = readdir(pd))) {
                    if (strncmp(pe->d_name, prefix, prefix_len) == 0) {
                        size_t len = strlen(d) + strlen(pe->d_name) + 2;
                        char *full = malloc(len);
                        if (!full) {
                            perror("malloc");
                            last_status = 1;
                            for (int j = 0; j < count; j++)
                                free(arr.items[j]);
                            free(arr.items);
                            closedir(pd);
                            free(pdup);
                            return NULL;
                        }
                        snprintf(full, len, "%s/%s", d, pe->d_name);
                        if (access(full, X_OK) == 0) {
                            if (!has_match(arr.items, count, pe->d_name)) {
                                if (count == cap) {
                                    cap *= 2;
                                    char **tmp = realloc(arr.items, cap * sizeof(char *));
                                    if (!tmp) {
                                        for (int j = 0; j < count; j++)
                                            free(arr.items[j]);
                                        free(arr.items);
                                        closedir(pd);
                                        free(pdup);
                                        free(full);
                                        return NULL;
                                    }
                                    arr.items = tmp; arr.capacity = cap;
                                }
                                char *dup = strdup(pe->d_name);
                                if (!dup) {
                                    for (int j = 0; j < count; j++)
                                        free(arr.items[j]);
                                    free(arr.items);
                                    closedir(pd);
                                    free(pdup);
                                    free(full);
                                    return NULL;
                                }
                                arr.items[count++] = dup; arr.count = count;
                                found = 1;
                            }
                        }
                        free(full);
                    }
                }
                closedir(pd);
                if (found || !arr.items)
                    break;
                }
            if (found)
                break;
            dir = strtok_r(NULL, ":", &saveptr);
        }
        free(pdup);
    }

    if (countp)
        *countp = arr.items ? count : 0;
    return arr.items;
}

/* Insert the completed text into the buffer and redraw the line. */
static void apply_completion(const char *match, char *buf, int *lenp, int *posp,
                             int start, const char *prompt, int *disp_lenp) {
    int mlen = strlen(match);
    int prefix_len = *posp - start;
    if (*lenp + mlen - prefix_len < MAX_LINE - 1) {
        memmove(&buf[start + mlen], &buf[*posp], *lenp - *posp);
        memcpy(&buf[start], match, mlen);
        *lenp += mlen - prefix_len;
        *posp = start + mlen;
        buf[*lenp] = '\0';
        int len = *lenp;
        printf("\r%s%s", prompt, buf);
        if (*disp_lenp > len) {
            for (int i = len; i < *disp_lenp; i++)
                putchar(' ');
            printf("\r%s%s", prompt, buf);
        }
        for (int i = len; i > *posp; i--)
            printf("\b");
        fflush(stdout);
        *disp_lenp = len;
    }
}

void handle_completion(const char *prompt, char *buf, int *lenp, int *posp,
                       int *disp_lenp) {
    int start = *posp;
    while (start > 0 && buf[start - 1] != ' ' && buf[start - 1] != '\t')
        start--;

    char prefix[MAX_LINE];
    memcpy(prefix, &buf[start], *posp - start);
    prefix[*posp - start] = '\0';

    int bcount = 0;
    char **bmatches = collect_builtin_matches(prefix, *posp - start, &bcount);
    if (!bmatches)
        return;

    /* If there's only one builtin match, use it immediately.  */
    if (bcount == 1) {
        apply_completion(bmatches[0], buf, lenp, posp, start, prompt,
                         disp_lenp);
        free(bmatches[0]);
        free(bmatches);
        return;
    }

    int pcount = 0;
    char **pmatches = collect_matches(prefix, *posp - start, &pcount);
    if (!pmatches && pcount > 0) {
        for (int i = 0; i < bcount; i++)
            free(bmatches[i]);
        free(bmatches);
        return;
    }

    int cap = bcount + pcount + 1;
    char **matches = malloc(cap * sizeof(char *));
    if (!matches) {
        perror("malloc");
        last_status = 1;
        for (int i = 0; i < bcount; i++)
            free(bmatches[i]);
        free(bmatches);
        if (pmatches) {
            for (int i = 0; i < pcount; i++)
                free(pmatches[i]);
            free(pmatches);
        }
        return;
    }

    int mcount = 0;
    for (int i = 0; i < bcount; i++)
        matches[mcount++] = bmatches[i];

    if (pmatches) {
        for (int i = 0; i < pcount; i++) {
            if (!has_match(matches, mcount, pmatches[i]))
                matches[mcount++] = pmatches[i];
            else
                free(pmatches[i]);
        }
        free(pmatches);
    }
    free(bmatches);

    if (mcount == 0) {
        free(matches);
        return;
    }

    if (mcount == 1) {
        apply_completion(matches[0], buf, lenp, posp, start, prompt, disp_lenp);
    } else {
        qsort(matches, mcount, sizeof(char *), cmpstr);
        printf("\r\n");
        for (int i = 0; i < mcount; i++)
            printf("%s%s", matches[i], i == mcount - 1 ? "" : " ");
        printf("\r\n");
        printf("\r%s%s", prompt, buf);
        fflush(stdout);
        if (*lenp > *disp_lenp)
            *disp_lenp = *lenp;
    }
    for (int i = 0; i < mcount; i++)
        free(matches[i]);
    free(matches);
}

