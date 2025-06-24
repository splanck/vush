/*
 * vush - a simple UNIX shell
 * Licensed under the BSD 2-Clause Simplified License.
 * Alias builtin commands and persistence.
 */

/*
 * Alias builtins and helpers.
 *
 * Aliases are stored in a simple list of `struct alias_entry`
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
#include "state_paths.h"
#include "error.h"
#include "list.h"

struct alias_entry {
    char *name;
    char *value;
    ListNode node;
};

static List aliases;

static int set_alias(const char *name, const char *value);
static void remove_all_aliases(const char *name);

/* Write the current alias list to the file returned by get_alias_file(). */
static void save_aliases(void)
{
    char *path = get_alias_file();
    if (!path) {
        fprintf(stderr, "warning: unable to determine alias file location\n");
        return;
    }
    FILE *f = fopen(path, "w");
    free(path);
    if (!f)
        return;
    LIST_FOR_EACH(n, &aliases) {
        struct alias_entry *a = LIST_ENTRY(n, struct alias_entry, node);
        fprintf(f, "%s=%s\n", a->name, a->value);
    }
    fclose(f);
}

/* Populate the alias list from the alias file if it exists. */
void load_aliases(void)
{
    list_init(&aliases);
    char *path = get_alias_file();
    if (!path) {
        fprintf(stderr, "warning: unable to determine alias file location\n");
        return;
    }
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
    LIST_FOR_EACH(n, &aliases) {
        struct alias_entry *a = LIST_ENTRY(n, struct alias_entry, node);
        if (strcmp(a->name, name) == 0)
            return a->value;
    }
    return NULL;
}

/* Create or update an alias. */
static int set_alias(const char *name, const char *value)
{
    if (strchr(value, '\n') || strchr(value, '=')) {
        fprintf(stderr, "alias: invalid value for %s\n", name);
        return -1;
    }
    /* If an alias with this NAME already exists make sure it is fully
     * removed so the new definition completely replaces it.  This avoids
     * duplicate entries which could cause the old value to be printed when
     * querying the alias list. */
    remove_all_aliases(name);

    struct alias_entry *new_alias = xmalloc(sizeof(struct alias_entry));
    new_alias->name = xstrdup(name);
    new_alias->value = xstrdup(value);
    list_append(&aliases, &new_alias->node);
    return 0;
}

/* Remove NAME from the alias list if present. */
static void remove_alias(const char *name)
{
    LIST_FOR_EACH(n, &aliases) {
        struct alias_entry *a = LIST_ENTRY(n, struct alias_entry, node);
        if (strcmp(a->name, name) == 0) {
            list_remove(&aliases, &a->node);
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
    ListNode *n = aliases.head;
    while (n) {
        ListNode *next = n->next;
        struct alias_entry *a = LIST_ENTRY(n, struct alias_entry, node);
        if (strcmp(a->name, name) == 0) {
            list_remove(&aliases, &a->node);
            free(a->name);
            free(a->value);
            free(a);
        }
        n = next;
    }
}

/* Print all defined aliases to stdout. */
static int printed_before(const char *name, struct alias_entry *limit)
{
    LIST_FOR_EACH(n, &aliases) {
        struct alias_entry *b = LIST_ENTRY(n, struct alias_entry, node);
        if (b == limit)
            break;
        if (strcmp(b->name, name) == 0)
            return 1;
    }
    return 0;
}

static void list_aliases_fmt(const char *fmt)
{
    LIST_FOR_EACH(n, &aliases) {
        struct alias_entry *a = LIST_ENTRY(n, struct alias_entry, node);
        if (!printed_before(a->name, a))
            printf(fmt, a->name, a->value);
    }
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
    ListNode *n = aliases.head;
    while (n) {
        ListNode *next = n->next;
        struct alias_entry *a = LIST_ENTRY(n, struct alias_entry, node);
        free(a->name);
        free(a->value);
        free(a);
        n = next;
    }
    list_init(&aliases);
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
            fprintf(stderr, "usage: alias name=value\n");
            continue;
        }
        *eq = '\0';
        const char *name = args[i];
        const char *value = eq + 1;
        remove_all_aliases(name);
        if (set_alias(name, value) < 0) {
            *eq = '=';
            fprintf(stderr, "alias: failed to set %s\n", name);
            continue;
        }
        printf("%s='%s'\n", name, value);
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
            fprintf(stderr, "usage: unalias [-a] name\n");
            return 1;
        }
    }

    if (all && args[i]) {
        fprintf(stderr, "usage: unalias [-a] name\n");
        return 1;
    }

    if (all) {
        free_aliases();
        return 1;
    }

    if (!args[i]) {
        fprintf(stderr, "usage: unalias [-a] name\n");
        return 1;
    }

    for (; args[i]; i++)
        remove_alias(args[i]);
    save_aliases();
    return 1;
}

