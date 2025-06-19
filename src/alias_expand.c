#define _GNU_SOURCE
#include "alias_expand.h"
#include "builtins.h"
#include <string.h>
#include <stdlib.h>

#define MAX_ALIAS_DEPTH 10

static int collect_alias_tokens(const char *name, char **out, int *count,
                                char visited[][MAX_LINE], int depth) {
    if (*count >= MAX_TOKENS - 1)
        return 0;

    int start = *count;

    if (depth >= MAX_ALIAS_DEPTH) {
        char *cp = strdup(name);
        if (!cp)
            return -1;
        out[(*count)++] = cp;
        return 0;
    }

    for (int i = 0; i < depth; i++) {
        if (strcmp(visited[i], name) == 0) {
            char *cp = strdup(name);
            if (!cp)
                return -1;
            out[(*count)++] = cp;
            return 0;
        }
    }

    const char *alias = get_alias(name);
    if (!alias) {
        char *cp = strdup(name);
        if (!cp)
            return -1;
        out[(*count)++] = cp;
        return 0;
    }

    strncpy(visited[depth], name, MAX_LINE);
    visited[depth][MAX_LINE - 1] = '\0';

    char *dup = strdup(alias);
    if (!dup)
        return -1;

    char *sp = NULL;
    char *word = strtok_r(dup, " \t", &sp);
    if (!word) {
        free(dup);
        return 0;
    }

    if (collect_alias_tokens(word, out, count, visited, depth + 1) == -1) {
        free(dup);
        goto error;
    }

    word = strtok_r(NULL, " \t", &sp);
    while (word && *count < MAX_TOKENS - 1) {
        char *cp = strdup(word);
        if (!cp) {
            free(dup);
            goto error;
        }
        out[(*count)++] = cp;
        word = strtok_r(NULL, " \t", &sp);
    }

    free(dup);
    return 0;

error:
    for (int i = start; i < *count; i++)
        free(out[i]);
    *count = start;
    return -1;
}

int expand_aliases_in_segment(PipelineSegment *seg, int *argc, char *tok) {
    const char *alias = get_alias(tok);
    if (!alias)
        return 0;

    char *orig = tok;
    char *tokens[MAX_TOKENS];
    int count = 0;
    char visited[MAX_ALIAS_DEPTH][MAX_LINE];
    memset(visited, 0, sizeof(visited));

    if (collect_alias_tokens(orig, tokens, &count, visited, 0) == -1) {
        free(orig);
        for (int i = 0; i < count; i++)
            free(tokens[i]);
        return -1;
    }

    if (count == 0) {
        free(orig);
        return 0;
    }

    free(orig);
    int i = 0;
    for (; i < count && *argc < MAX_TOKENS - 1; i++) {
        seg->argv[*argc] = tokens[i];
        seg->expand[*argc] = 1;
        seg->quoted[*argc] = 0;
        (*argc)++;
    }
    for (; i < count; i++)
        free(tokens[i]);
    return 1;
}
