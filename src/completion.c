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
#include <unistd.h>
#include <limits.h>

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

/* Collect builtin and filesystem matches for the given prefix. */
static char **collect_matches(const char *prefix, int prefix_len, int *countp) {
    int count = 0;
    int cap = 16;
    char **matches = malloc(cap * sizeof(char *));
    if (!matches)
        matches = NULL;

    const char **bn = get_builtin_names();
    if (matches)
    for (int i = 0; bn[i]; i++) {
        if (strncmp(bn[i], prefix, prefix_len) == 0) {
            if (has_match(matches, count, bn[i]))
                continue;
            if (count == cap) {
                cap *= 2;
                char **tmp = realloc(matches, cap * sizeof(char *));
                if (!tmp) {
                    for (int j = 0; j < count; j++)
                        free(matches[j]);
                    free(matches);
                    return NULL;
                }
                matches = tmp;
            }
            char *dup = strdup(bn[i]);
            if (!dup) {
                for (int j = 0; j < count; j++)
                    free(matches[j]);
                free(matches);
                return NULL;
            }
            matches[count++] = dup;
        }
    }

    DIR *d = opendir(".");
    if (d && matches) {
        struct dirent *de;
        while ((de = readdir(d))) {
            if (strncmp(de->d_name, prefix, prefix_len) == 0) {
                if (has_match(matches, count, de->d_name))
                    continue;
                if (count == cap) {
                    cap *= 2;
                    char **tmp = realloc(matches, cap * sizeof(char *));
                    if (!tmp) {
                        for (int j = 0; j < count; j++)
                            free(matches[j]);
                        free(matches);
                        closedir(d);
                        return NULL;
                    }
                    matches = tmp;
                }
                char *dup = strdup(de->d_name);
                if (!dup) {
                    for (int j = 0; j < count; j++)
                        free(matches[j]);
                    free(matches);
                    closedir(d);
                    return NULL;
                }
                matches[count++] = dup;
            }
        }
        closedir(d);
    }

    const char *path = getenv("PATH");
    if (path && matches) {
        char *pdup = strdup(path);
        if (!pdup) {
            for (int j = 0; j < count; j++)
                free(matches[j]);
            free(matches);
            return NULL;
        }
        char *saveptr = NULL;
        char *dir = strtok_r(pdup, ":", &saveptr);
        while (dir) {
            DIR *pd = opendir(dir);
            if (pd) {
                struct dirent *pe;
                while ((pe = readdir(pd))) {
                        if (strncmp(pe->d_name, prefix, prefix_len) == 0) {
                            if (has_match(matches, count, pe->d_name))
                                continue;
                            char full[PATH_MAX];
                            snprintf(full, sizeof(full), "%s/%s", dir, pe->d_name);
                            if (access(full, X_OK) == 0) {
                                if (count == cap) {
                                    cap *= 2;
                                    char **tmp = realloc(matches, cap * sizeof(char *));
                                    if (!tmp) {
                                        for (int j = 0; j < count; j++)
                                            free(matches[j]);
                                        free(matches);
                                        closedir(pd);
                                        free(pdup);
                                        return NULL;
                                    }
                                    matches = tmp;
                                }
                                char *dup = strdup(pe->d_name);
                                if (!dup) {
                                    for (int j = 0; j < count; j++)
                                        free(matches[j]);
                                    free(matches);
                                    closedir(pd);
                                    free(pdup);
                                    return NULL;
                                }
                                matches[count++] = dup;
                            }
                        }
                    }
                    closedir(pd);
                    if (!matches)
                        break;
                }
                dir = strtok_r(NULL, ":", &saveptr);
        }
        free(pdup);
    }

    if (countp)
        *countp = matches ? count : 0;
    return matches;
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
        printf("\r%s%s", prompt, buf);
        if (*lenp > *disp_lenp)
            *disp_lenp = *lenp;
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

    int mcount = 0;
    char **matches = collect_matches(prefix, *posp - start, &mcount);
    if (!matches)
        return;

    if (mcount == 1) {
        apply_completion(matches[0], buf, lenp, posp, start, prompt, disp_lenp);
    } else if (mcount > 1) {
        qsort(matches, mcount, sizeof(char *), cmpstr);
        printf("\r\n");
        for (int i = 0; i < mcount; i++)
            printf("%s%s", matches[i], i == mcount - 1 ? "" : " ");
        printf("\r\n");
        printf("\r%s%s", prompt, buf);
        if (*lenp > *disp_lenp)
            *disp_lenp = *lenp;
    }
    for (int i = 0; i < mcount; i++)
        free(matches[i]);
    free(matches);
}

