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
#include <fcntl.h>
#include "util.h"
#include "shell_state.h"

struct hash_entry {
    char *name;
    char *path;
    int fd;
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
        if (xasprintf(&full, "%s/%s", d, name) < 0) {
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

const char *hash_lookup(const char *name, int *fd) {
    for (struct hash_entry *e = hash_list; e; e = e->next) {
        if (strcmp(e->name, name) == 0) {
            if (fd)
                *fd = e->fd;
            return e->path;
        }
    }
    return NULL;
}

int hash_add(const char *name) {
    if (strchr(name, '/'))
        return -1;
    if (hash_lookup(name, NULL))
        return 0;
    char *path = search_path(name);
    if (!path)
        return -1;
    char *resolved = realpath(path, NULL);
    if (resolved) {
        free(path);
        path = resolved;
    }
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        free(path);
        return -1;
    }
    struct hash_entry *e = malloc(sizeof(struct hash_entry));
    if (!e) {
        /* report failure when allocating the hash entry */
        perror("malloc");
        last_status = 1;
        free(path);
        close(fd);
        return -1;
    }
    e->name = strdup(name);
    if (!e->name) {
        perror("strdup");
        last_status = 1;
        free(path);
        close(fd);
        free(e);
        return -1;
    }
    e->path = path;
    e->fd = fd;
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
        if (e->fd >= 0)
            close(e->fd);
        free(e);
        e = n;
    }
    hash_list = NULL;
}

void hash_print(void) {
    for (struct hash_entry *e = hash_list; e; e = e->next)
        printf("%s %s\n", e->name, e->path);
}

int hash_add_path(const char *name, const char *path) {
    if (!name || !path)
        return -1;
    if (strchr(name, '/'))
        return -1;
    if (hash_lookup(name, NULL))
        return 0;
    char *real = realpath(path, NULL);
    char *p = real ? real : strdup(path);
    if (!p)
        return -1;
    int fd = open(p, O_RDONLY);
    if (fd < 0) {
        free(p);
        return -1;
    }
    struct hash_entry *e = malloc(sizeof(struct hash_entry));
    if (!e) {
        free(p);
        close(fd);
        return -1;
    }
    e->name = strdup(name);
    if (!e->name) {
        perror("strdup");
        last_status = 1;
        free(p);
        close(fd);
        free(e);
        return -1;
    }
    e->path = p;
    e->fd = fd;
    e->next = hash_list;
    hash_list = e;
    return 0;
}

void hash_remove(const char *name) {
    struct hash_entry **pp = &hash_list;
    while (*pp) {
        struct hash_entry *e = *pp;
        if (strcmp(e->name, name) == 0) {
            *pp = e->next;
            free(e->name);
            free(e->path);
            if (e->fd >= 0)
                close(e->fd);
            free(e);
            return;
        }
        pp = &(*pp)->next;
    }
}
