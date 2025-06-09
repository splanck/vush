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

void handle_completion(const char *prompt, char *buf, int *lenp, int *posp,
                       int *disp_lenp) {
    int start = *posp;
    while (start > 0 && buf[start - 1] != ' ' && buf[start - 1] != '\t')
        start--;

    char prefix[MAX_LINE];
    memcpy(prefix, &buf[start], *posp - start);
    prefix[*posp - start] = '\0';

    int mcount = 0;
    int mcap = 16;
    char **matches = malloc(mcap * sizeof(char *));
    if (!matches)
        matches = NULL;
    const char **bn = get_builtin_names();
    if (matches)
    for (int i = 0; bn[i]; i++) {
        if (strncmp(bn[i], prefix, *posp - start) == 0) {
            if (mcount == mcap) {
                mcap *= 2;
                matches = realloc(matches, mcap * sizeof(char *));
                if (!matches) break;
            }
            matches[mcount++] = strdup(bn[i]);
        }
    }
    DIR *d = opendir(".");
    if (d && matches) {
        struct dirent *de;
        while ((de = readdir(d))) {
            if (strncmp(de->d_name, prefix, *posp - start) == 0) {
                if (mcount == mcap) {
                    mcap *= 2;
                    matches = realloc(matches, mcap * sizeof(char *));
                    if (!matches) break;
                }
                matches[mcount++] = strdup(de->d_name);
            }
        }
        closedir(d);
    }
    if (matches) {
        if (mcount == 1) {
            const char *match = matches[0];
            int mlen = strlen(match);
            if (*lenp + mlen - (*posp - start) < MAX_LINE - 1) {
                memmove(&buf[start + mlen], &buf[*posp], *lenp - *posp);
                memcpy(&buf[start], match, mlen);
                *lenp += mlen - (*posp - start);
                *posp = start + mlen;
                buf[*lenp] = '\0';
                printf("\r%s%s", prompt, buf);
                if (*lenp > *disp_lenp)
                    *disp_lenp = *lenp;
            }
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
}

