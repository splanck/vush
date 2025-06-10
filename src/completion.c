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

static int cmpstr(const void *a, const void *b) {
    const char *aa = *(const char **)a;
    const char *bb = *(const char **)b;
    return strcmp(aa, bb);
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
            if (count == cap) {
                cap *= 2;
                matches = realloc(matches, cap * sizeof(char *));
                if (!matches) break;
            }
            matches[count++] = strdup(bn[i]);
        }
    }

    DIR *d = opendir(".");
    if (d && matches) {
        struct dirent *de;
        while ((de = readdir(d))) {
            if (strncmp(de->d_name, prefix, prefix_len) == 0) {
                if (count == cap) {
                    cap *= 2;
                    matches = realloc(matches, cap * sizeof(char *));
                    if (!matches) break;
                }
                matches[count++] = strdup(de->d_name);
            }
        }
        closedir(d);
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

