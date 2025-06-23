/*
 * Directory stack implementation for pushd/popd.
 *
 * The shell maintains a simple stack of directory paths so that the
 * `pushd` and `popd` builtins can save and restore the working directory.
 * Each entry is stored in a singly linked list with the newest directory at
 * the head.  `pushd` records the current directory by calling
 * dirstack_push(), while `popd` retrieves the most recently saved location
 * via dirstack_pop().  The stack can be displayed with the `dirs` builtin
 * which relies on dirstack_print().
 */
#define _GNU_SOURCE
#include "dirstack.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

typedef struct DirNode {
    char *dir;
    struct DirNode *next;
} DirNode;

static DirNode *top = NULL;

/*
 * Push a directory path onto the stack.  The path is duplicated so the
 * caller may free its copy.  If memory allocation fails the stack is left
 * unchanged.
 */
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

/*
 * Pop the most recently saved directory path.
 * Returns a malloc'd string that the caller must free, or NULL when the
 * stack is empty.
 */
char *dirstack_pop(void) {
    if (!top)
        return NULL;
    DirNode *n = top;
    top = n->next;
    char *dir = n->dir;
    free(n);
    return dir;
}

/*
 * Print the contents of the directory stack from newest to oldest.
 * Each entry is separated by a space and a trailing newline is added.
 */
void dirstack_print(void) {
    const char *pwd;
    char *cwd = getcwd(NULL, 0);
    if (cwd) {
        pwd = cwd;
    } else {
        pwd = getenv("PWD");
        if (!pwd)
            pwd = "";
    }

    printf("%s ", pwd);

    DirNode *n = top;
    while (n) {
        printf("%s ", n->dir);
        n = n->next;
    }
    printf("\n");
    free(cwd);
}

/* Free all directory stack entries. */
void dirstack_clear(void) {
    DirNode *n = top;
    while (n) {
        DirNode *next = n->next;
        free(n->dir);
        free(n);
        n = next;
    }
    top = NULL;
}

/*
 * Update the PWD and OLDPWD environment variables after a directory
 * change. The old working directory should be passed in so it can be
 * stored in OLDPWD. The current directory is retrieved via getcwd and
 * assigned to PWD.  If getcwd fails the environment is left unchanged.
 */
void update_pwd_env(const char *oldpwd) {
    char *cwd = getcwd(NULL, 0);
    if (!cwd)
        return;
    if (!oldpwd)
        oldpwd = "";
    setenv("OLDPWD", oldpwd, 1);
    setenv("PWD", cwd, 1);
    free(cwd);
}
