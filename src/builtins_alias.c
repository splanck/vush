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
#include "util.h"

struct alias_entry {
    char *name;
    char *value;
    struct alias_entry *next;
};

static struct alias_entry *aliases = NULL;

static int set_alias(const char *name, const char *value);
static void remove_all_aliases(const char *name);

/* Return the path of the alias file or NULL if $HOME is not set. */
static char *aliasfile_path(void)
{
    return make_user_path("VUSH_ALIASFILE", ".vush_aliases");
}

/* Write the current alias list to the file returned by aliasfile_path(). */
static void save_aliases(void)
{
    char *path = aliasfile_path();
    if (!path)
        return;
    FILE *f = fopen(path, "w");
    free(path);
    if (!f)
        return;
    for (struct alias_entry *a = aliases; a; a = a->next)
        fprintf(f, "%s=%s\n", a->name, a->value);
    fclose(f);
}

/* Populate the alias list from the alias file if it exists. */
void load_aliases(void)
{
    char *path = aliasfile_path();
    if (!path)
        return;
    FILE *f = fopen(path, "r");
    free(path);
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
    /* If an alias with this NAME already exists make sure it is fully
     * removed so the new definition completely replaces it.  This avoids
     * duplicate entries which could cause the old value to be printed when
     * querying the alias list. */
    remove_all_aliases(name);

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
    new_alias->next = NULL;
    if (!aliases) {
        aliases = new_alias;
    } else {
        struct alias_entry *tail = aliases;
        while (tail->next)
            tail = tail->next;
        tail->next = new_alias;
    }
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

/* Remove all entries matching NAME from the alias list. */
static void remove_all_aliases(const char *name)
{
    struct alias_entry *a = aliases;
    struct alias_entry *prev = NULL;
    while (a) {
        if (strcmp(a->name, name) == 0) {
            struct alias_entry *next = a->next;
            if (prev)
                prev->next = next;
            else
                aliases = next;
            free(a->name);
            free(a->value);
            free(a);
            a = next;
            continue;
        }
        prev = a;
        a = a->next;
    }
}

/* Print all defined aliases to stdout. */
static int printed_before(const char *name, struct alias_entry *limit)
{
    for (struct alias_entry *b = aliases; b && b != limit; b = b->next)
        if (strcmp(b->name, name) == 0)
            return 1;
    return 0;
}

static void list_aliases_fmt(const char *fmt)
{
    for (struct alias_entry *a = aliases; a; a = a->next)
        if (!printed_before(a->name, a))
            printf(fmt, a->name, a->value);
}

static void list_aliases(void)
{
    list_aliases_fmt("%s='%s'\n");
}

static void list_aliases_p(void)
{
    list_aliases_fmt("alias %s='%s'\n");
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

    if (strcmp(args[1], "-p") == 0 && !args[2]) {
        list_aliases_p();
        return 1;
    }

    if (!args[2]) {
        char *eq = strchr(args[1], '=');
        if (!eq) {
            const char *val = get_alias(args[1]);
            if (val)
                printf("%s='%s'\n", args[1], val);
            else
                fprintf(stderr, "alias: %s: not found\n", args[1]);
            return 1;
        } else {
            /* Some line editors expand aliases before execution which can
             * transform `alias name` into `alias name=value`.  If the value
             * matches the current alias definition treat this as a query
             * rather than a new assignment so scripts still work. */
            *eq = '\0';
            const char *name = args[1];
            const char *val = get_alias(name);

            const char *newval = eq + 1;
            char *buf = NULL;
            if (val) {
                size_t len = strlen(newval);
                if (len >= 2 &&
                    ((newval[0] == '\'' && newval[len - 1] == '\'') ||
                     (newval[0] == '"' && newval[len - 1] == '"'))) {
                    buf = strndup(newval + 1, len - 2);
                    if (!buf) {
                        *eq = '=';
                        perror("strndup");
                        return 1;
                    }
                    newval = buf;
                }
            }
            int same = val && strcmp(val, newval) == 0;
            free(buf);
            *eq = '=';
            if (same) {
                printf("%s='%s'\n", name, val);
                return 1;
            }
        }
    }

    for (int i = 1; args[i]; i++) {
        char *eq = strchr(args[i], '=');
        if (!eq) {
            usage_error("alias name=value");
            continue;
        }
        *eq = '\0';
        remove_all_aliases(args[i]);
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
    int all = 0;
    int i = 1;
    for (; args[i] && args[i][0] == '-'; i++) {
        if (strcmp(args[i], "-a") == 0) {
            all = 1;
        } else {
            return usage_error("unalias [-a] name");
        }
    }

    if (all && args[i]) {
        return usage_error("unalias [-a] name");
    }

    if (all) {
        free_aliases();
        return 1;
    }

    if (!args[i]) {
        return usage_error("unalias [-a] name");
    }

    for (; args[i]; i++)
        remove_alias(args[i]);
    save_aliases();
    return 1;
}

