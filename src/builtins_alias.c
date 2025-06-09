/* Alias builtins and helpers */
#define _GNU_SOURCE
#include "builtins.h"
#include "parser.h" /* for MAX_LINE */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <linux/limits.h>

struct alias_entry {
    char *name;
    char *value;
    struct alias_entry *next;
};

static struct alias_entry *aliases = NULL;

static void set_alias(const char *name, const char *value);

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
        set_alias(line, eq + 1);
    }
    fclose(f);
}

const char *get_alias(const char *name)
{
    for (struct alias_entry *a = aliases; a; a = a->next) {
        if (strcmp(a->name, name) == 0)
            return a->value;
    }
    return NULL;
}

static void set_alias(const char *name, const char *value)
{
    for (struct alias_entry *a = aliases; a; a = a->next) {
        if (strcmp(a->name, name) == 0) {
            free(a->value);
            a->value = strdup(value);
            return;
        }
    }
    struct alias_entry *new_alias = malloc(sizeof(struct alias_entry));
    if (!new_alias) {
        perror("malloc");
        return;
    }
    new_alias->name = strdup(name);
    new_alias->value = strdup(value);
    new_alias->next = aliases;
    aliases = new_alias;
}

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

static void list_aliases(void)
{
    for (struct alias_entry *a = aliases; a; a = a->next)
        printf("%s='%s'\n", a->name, a->value);
}

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
        set_alias(args[i], eq + 1);
        *eq = '=';
    }
    save_aliases();
    return 1;
}

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

