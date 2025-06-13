/*
 * Simple command hashing for faster lookups.
 */
#define _GNU_SOURCE
#include "hash.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <limits.h>

struct hash_entry {
    char *name;
    char *path;
    struct hash_entry *next;
};

static struct hash_entry *hash_list = NULL;

/* Search PATH for NAME and return a newly allocated full path or NULL. */
static char *search_path(const char *name) {
    const char *pathenv = getenv("PATH");
    if (!pathenv || !*pathenv)
        pathenv = "/bin:/usr/bin";
    char *paths = strdup(pathenv);
    if (!paths)
        return NULL;
    char *save = NULL;
    char *dir = strtok_r(paths, ":", &save);
    char *result = NULL;
    while (dir) {
        const char *d = *dir ? dir : ".";
        char *full = NULL;
        if (asprintf(&full, "%s/%s", d, name) < 0) {
            result = NULL;
            break;
        }
        if (access(full, X_OK) == 0) {
            result = full;
            break;
        }
        free(full);
        dir = strtok_r(NULL, ":", &save);
    }
    free(paths);
    return result;
}

const char *hash_lookup(const char *name) {
    for (struct hash_entry *e = hash_list; e; e = e->next) {
        if (strcmp(e->name, name) == 0)
            return e->path;
    }
    return NULL;
}

int hash_add(const char *name) {
    if (strchr(name, '/'))
        return -1;
    if (hash_lookup(name))
        return 0;
    char *path = search_path(name);
    if (!path)
        return -1;
    struct hash_entry *e = malloc(sizeof(struct hash_entry));
    if (!e) {
        free(path);
        return -1;
    }
    e->name = strdup(name);
    if (!e->name) {
        free(path);
        free(e);
        return -1;
    }
    e->path = path;
    e->next = hash_list;
    hash_list = e;
    return 0;
}

void hash_clear(void) {
    struct hash_entry *e = hash_list;
    while (e) {
        struct hash_entry *n = e->next;
        free(e->name);
        free(e->path);
        free(e);
        e = n;
    }
    hash_list = NULL;
}

void hash_print(void) {
    for (struct hash_entry *e = hash_list; e; e = e->next)
        printf("%s %s\n", e->name, e->path);
}
