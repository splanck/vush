/*
 * Alias builtins and helpers.
 *
 * Aliases are stored in a simple linked list of `struct alias_entry`
 * nodes.  At startup `load_aliases()` reads the file specified by the
 * `VUSH_ALIASFILE` environment variable or `~/.vush_aliases` when the
 * variable is unset.  Each line in the file contains a single
 * `name=value` pair.  When the shell terminates `free_aliases()` writes
 * the current list back with `save_aliases()` so aliases persist across
 * sessions.
 */
#define _GNU_SOURCE
#include "builtins.h"
#include "parser.h" /* for MAX_LINE */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

struct alias_entry {
    char *name;
    char *value;
    struct alias_entry *next;
};

static struct alias_entry *aliases = NULL;

static int set_alias(const char *name, const char *value);

/* Return the path of the alias file or NULL if $HOME is not set. */
static const char *aliasfile_path(void)
{
    const char *env = getenv("VUSH_ALIASFILE");
    if (env && *env)
        return env;
    const char *home = getenv("HOME");
    if (!home)
        return NULL;
    static char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/.vush_aliases", home);
    return path;
}

/* Write the current alias list to the file returned by aliasfile_path(). */
static void save_aliases(void)
{
    const char *path = aliasfile_path();
    if (!path)
        return;
    FILE *f = fopen(path, "w");
    if (!f)
        return;
    for (struct alias_entry *a = aliases; a; a = a->next)
        fprintf(f, "%s=%s\n", a->name, a->value);
    fclose(f);
}

/* Populate the alias list from the alias file if it exists. */
void load_aliases(void)
{
    const char *path = aliasfile_path();
    if (!path)
        return;
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    char line[MAX_LINE];
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        if (len && line[len - 1] == '\n')
            line[len - 1] = '\0';
        char *eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = '\0';
        if (set_alias(line, eq + 1) < 0) {
            fclose(f);
            return;
        }
    }
    fclose(f);
}

/* Find the value for NAME or return NULL if it is not defined. */
const char *get_alias(const char *name)
{
    for (struct alias_entry *a = aliases; a; a = a->next) {
        if (strcmp(a->name, name) == 0)
            return a->value;
    }
    return NULL;
}

/* Create or update an alias.  Returns 0 on success, -1 on allocation failure. */
static int set_alias(const char *name, const char *value)
{
    for (struct alias_entry *a = aliases; a; a = a->next) {
        if (strcmp(a->name, name) == 0) {
            char *dup = strdup(value);
            if (!dup)
                return -1;
            free(a->value);
            a->value = dup;
            return 0;
        }
    }
    struct alias_entry *new_alias = malloc(sizeof(struct alias_entry));
    if (!new_alias) {
        perror("malloc");
        return -1;
    }
    new_alias->name = strdup(name);
    if (!new_alias->name) {
        free(new_alias);
        return -1;
    }
    new_alias->value = strdup(value);
    if (!new_alias->value) {
        free(new_alias->name);
        free(new_alias);
        return -1;
    }
    new_alias->next = aliases;
    aliases = new_alias;
    return 0;
}

/* Remove NAME from the alias list if present. */
static void remove_alias(const char *name)
{
    struct alias_entry *prev = NULL;
    for (struct alias_entry *a = aliases; a; prev = a, a = a->next) {
        if (strcmp(a->name, name) == 0) {
            if (prev)
                prev->next = a->next;
            else
                aliases = a->next;
            free(a->name);
            free(a->value);
            free(a);
            return;
        }
    }
}

/* Print all defined aliases to stdout. */
static void list_aliases(void)
{
    for (struct alias_entry *a = aliases; a; a = a->next)
        printf("%s='%s'\n", a->name, a->value);
}

/* Save aliases and free all alias list entries. */
void free_aliases(void)
{
    save_aliases();
    struct alias_entry *a = aliases;
    while (a) {
        struct alias_entry *next = a->next;
        free(a->name);
        free(a->value);
        free(a);
        a = next;
    }
    aliases = NULL;
}

/* builtin_alias - list aliases or define name=value pairs. */
int builtin_alias(char **args)
{
    if (!args[1]) {
        list_aliases();
        return 1;
    }
    for (int i = 1; args[i]; i++) {
        char *eq = strchr(args[i], '=');
        if (!eq) {
            fprintf(stderr, "usage: alias name=value\n");
            continue;
        }
        *eq = '\0';
        if (set_alias(args[i], eq + 1) < 0) {
            *eq = '=';
            fprintf(stderr, "alias: failed to set %s\n", args[i]);
            continue;
        }
        *eq = '=';
    }
    save_aliases();
    return 1;
}

/* builtin_unalias - remove each NAME argument from the alias list. */
int builtin_unalias(char **args)
{
    if (!args[1]) {
        fprintf(stderr, "usage: unalias name\n");
        return 1;
    }
    for (int i = 1; args[i]; i++)
        remove_alias(args[i]);
    save_aliases();
    return 1;
}

