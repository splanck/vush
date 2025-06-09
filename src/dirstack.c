/*
 * Directory stack implementation for pushd/popd.
 */
#define _GNU_SOURCE
#include "dirstack.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef struct DirNode {
    char *dir;
    struct DirNode *next;
} DirNode;

static DirNode *top = NULL;

void dirstack_push(const char *dir) {
    DirNode *n = malloc(sizeof(DirNode));
    if (!n)
        return;
    n->dir = strdup(dir);
    if (!n->dir) {
        free(n);
        return;
    }
    n->next = top;
    top = n;
}

char *dirstack_pop(void) {
    if (!top)
        return NULL;
    DirNode *n = top;
    top = n->next;
    char *dir = n->dir;
    free(n);
    return dir;
}

void dirstack_print(void) {
    DirNode *n = top;
    while (n) {
        printf("%s ", n->dir);
        n = n->next;
    }
    printf("\n");
}
