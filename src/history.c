/*
 * Command history management routines.
 *
 * History is kept in a doubly linked list of ``HistEntry`` nodes.  New
 * commands are appended to ``tail`` and each entry is assigned an incrementing
 * identifier.  When the number of stored entries exceeds ``max_history`` the
 * oldest entry at ``head`` is discarded.  The history is optionally persisted
 * to ``$VUSH_HISTFILE`` (or ``$HOME/.vush_history`` if unset) so entries can be
 * reloaded across shell sessions.
 *
 * The interactive cursor (`cursor`) tracks navigation through the list for the
 * up/down history commands while `search_cursor` remembers the last match when
 * searching.  Searches walk the list in either direction looking for a command
 * substring.
*/
#define _GNU_SOURCE
#include "history.h"
#include "parser.h" /* for MAX_LINE */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "common.h"

typedef struct HistEntry {
    int id;
    char cmd[MAX_LINE];
    struct HistEntry *next;
    struct HistEntry *prev;
} HistEntry;

static HistEntry *head = NULL;
static HistEntry *tail = NULL;
static HistEntry *cursor = NULL;
static HistEntry *search_cursor = NULL;
static int next_id = 1;
static int skip_next = 0;
static int history_size = 0;
static int max_history = MAX_HISTORY;

/* Renumber history entries sequentially starting at 1. */
static void renumber_history(void)
{
    int id = 1;
    for (HistEntry *e = head; e; e = e->next)
        e->id = id++;
    next_id = id;
}

/*
 * Determine the path to the history file.  ``$VUSH_HISTFILE`` takes
 * precedence over ``$HOME/.vush_history``.  Returns NULL if no path can
 * be constructed.
 */
static const char *histfile_path(void) {
    const char *env = getenv("VUSH_HISTFILE");
    if (env && *env)
        return env;
    const char *home = getenv("HOME");
    if (!home)
        return NULL;
    static char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/.vush_history", home);
    return path;
}

/*
 * Initialise history settings from environment variables.  This function is
 * idempotent and may be called multiple times.
 */
static void history_init(void) {
    static int inited = 0;
    if (inited)
        return;
    const char *env = getenv("VUSH_HISTSIZE");
    if (env) {
        long val = strtol(env, NULL, 10);
        if (val > 0)
            max_history = (int)val;
    }
    inited = 1;
}

/*
 * Internal helper to append an entry to the history list.  When
 * ``save_file`` is non-zero the entry is also appended to the history file.
 */
static void add_history_entry(const char *cmd, int save_file) {
    history_init();
    HistEntry *e = malloc(sizeof(HistEntry));
    if (!e) return;
    e->id = next_id++;
    strncpy(e->cmd, cmd, MAX_LINE - 1);
    e->cmd[MAX_LINE - 1] = '\0';
    e->next = NULL;
    if (!head) {
        e->prev = NULL;
        head = tail = e;
    } else {
        tail->next = e;
        e->prev = tail;
        tail = e;
    }
    history_size++;

    if (history_size > max_history) {
        HistEntry *old = head;
        head = head->next;
        if (head)
            head->prev = NULL;
        else
            tail = NULL;
        free(old);
        history_size--;
    }

    if (save_file) {
        const char *path = histfile_path();
        if (path) {
            FILE *f = fopen(path, "a");
            if (f) {
                fprintf(f, "%s\n", cmd);
                fclose(f);
            }
        }
    }
}

/*
 * Record a new command in the history file and in-memory list.
 * Returns nothing.  If the global flag `skip_next` is set the command
 * is ignored and the flag cleared.
 */
void add_history(const char *cmd) {
    if (skip_next) {
        skip_next = 0;
        return;
    }
    add_history_entry(cmd, 1);
}

/*
 * Print the entire command history to stdout.
 * Returns nothing.
 */
void print_history(void) {
    for (HistEntry *e = head; e; e = e->next) {
        printf("%d %s\n", e->id, e->cmd);
    }
}

/*
 * Load history entries from the history file into memory.
 * Returns nothing if the file cannot be read.
 */
void load_history(void) {
    history_init();
    const char *path = histfile_path();
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
        add_history_entry(line, 0);
    }
    fclose(f);

    /*
     * If the history file contained more entries than allowed by
     * max_history older commands may have been dropped while loading.
     * Renumber the remaining entries so identifiers start at 1 for
     * the current session and next_id reflects the new list.
     */
    if (head)
        renumber_history();
}

/*
 * Step backwards through history and return the previous command.
 * Returns NULL when there is no earlier entry.
 */
const char *history_prev(void) {
    if (!tail)
        return NULL;
    if (!cursor)
        cursor = tail;
    else if (cursor->prev)
        cursor = cursor->prev;
    return cursor ? cursor->cmd : NULL;
}

/*
 * Move forward through history and return the next command.
 * Returns NULL when there is no later entry.
 */
const char *history_next(void) {
    if (!cursor)
        return NULL;
    if (cursor->next)
        cursor = cursor->next;
    else
        cursor = NULL;
    return cursor ? cursor->cmd : NULL;
}

/*
 * Reset the navigation cursor used by history_prev/history_next.
 * Returns nothing.
 */
void history_reset_cursor(void) {
    cursor = NULL;
}

/*
 * Search backward for a history entry containing 'term'.
 * Returns the matched command or NULL if none is found.  Subsequent calls
 * continue searching from the previous match.
 */
const char *history_search_prev(const char *term) {
    if (!term || !*term || !tail)
        return NULL;
    HistEntry *start = search_cursor ? search_cursor->prev : tail;
    for (HistEntry *e = start; e; e = e->prev) {
        if (strstr(e->cmd, term)) {
            search_cursor = e;
            return e->cmd;
        }
    }
    return NULL;
}

/*
 * Continue searching forward for 'term'.  Starts from the previous match
 * if one exists.  Returns the matched command or NULL if none is found.
 */
const char *history_search_next(const char *term) {
    if (!term || !*term || !head)
        return NULL;
    HistEntry *start = search_cursor ? search_cursor->next : head;
    for (HistEntry *e = start; e; e = e->next) {
        if (strstr(e->cmd, term)) {
            search_cursor = e;
            return e->cmd;
        }
    }
    return NULL;
}

/*
 * Clear the search cursor used by history_search_prev/next.
 * Returns nothing.
 */
void history_reset_search(void) {
    search_cursor = NULL;
}

/*
 * Remove all history entries from memory and truncate the history file.
 * Returns nothing.
 */
void clear_history(void) {
    HistEntry *e = head;
    while (e) {
        HistEntry *next = e->next;
        free(e);
        e = next;
    }
    head = tail = cursor = search_cursor = NULL;
    next_id = 1;
    history_size = 0;

    const char *path = histfile_path();
    if (path) {
        FILE *f = fopen(path, "w");
        if (f)
            fclose(f);
    }
}

/*
 * Delete the history entry with the given identifier.  The history file
 * is rewritten to reflect the removal.  Returns nothing.
 */
void delete_history_entry(int id) {
    history_init();
    HistEntry *e = head;
    while (e && e->id != id)
        e = e->next;
    if (!e)
        return;

    if (e->prev)
        e->prev->next = e->next;
    else
        head = e->next;

    if (e->next)
        e->next->prev = e->prev;
    else
        tail = e->prev;

    if (cursor == e)
        cursor = e->next;
    if (search_cursor == e)
        search_cursor = e->next;

    free(e);
    history_size--;

    renumber_history();

    const char *path = histfile_path();
    if (!path)
        return;
    FILE *f = fopen(path, "w");
    if (!f)
        return;
    for (HistEntry *h = head; h; h = h->next)
        fprintf(f, "%s\n", h->cmd);
    fclose(f);
}

/*
 * Return the most recently added command or NULL if history is empty.
 */
const char *history_last(void) {
    return tail ? tail->cmd : NULL;
}

/*
 * Search from the most recent entry backwards for one starting with 'prefix'.
 * Returns the matched command or NULL if none is found.
 */
const char *history_find_prefix(const char *prefix) {
    if (!prefix || !*prefix)
        return NULL;
    size_t len = strlen(prefix);
    for (HistEntry *e = tail; e; e = e->prev) {
        if (strncmp(e->cmd, prefix, len) == 0)
            return e->cmd;
    }
    return NULL;
}

/*
 * Retrieve the command with identifier ID or NULL if no such entry exists.
 */
const char *history_get_by_id(int id) {
    for (HistEntry *e = head; e; e = e->next) {
        if (e->id == id)
            return e->cmd;
    }
    return NULL;
}

/*
 * Return the command OFFSET entries back from the most recent one.  OFFSET of
 * 1 yields the last command, 2 the one before that and so on.  NULL is returned
 * when the requested entry does not exist.
 */
const char *history_get_relative(int offset) {
    if (offset <= 0)
        return NULL;
    HistEntry *e = tail;
    while (e && --offset > 0)
        e = e->prev;
    return e ? e->cmd : NULL;
}

/* Internal helper to duplicate the last word of CMD. */
static char *dup_last_word(const char *cmd) {
    const char *end = cmd + strlen(cmd);
    while (end > cmd && isspace((unsigned char)*(end - 1)))
        end--;
    const char *start = end;
    while (start > cmd && !isspace((unsigned char)*(start - 1)))
        start--;
    size_t len = (size_t)(end - start);
    char *res = malloc(len + 1);
    if (!res)
        return NULL;
    memcpy(res, start, len);
    res[len] = '\0';
    return res;
}

/* Duplicate all arguments of CMD after the first word. */
static char *dup_all_args(const char *cmd) {
    const char *p = cmd;
    while (*p && !isspace((unsigned char)*p))
        p++;
    while (isspace((unsigned char)*p))
        p++;
    return strdup(p);
}

/* Return the last argument from the most recent command or NULL. */
char *history_last_word(void) {
    const char *cmd = history_get_relative(1);
    if (!cmd)
        return NULL;
    return dup_last_word(cmd);
}

/* Return all arguments from the most recent command or NULL. */
char *history_all_words(void) {
    const char *cmd = history_get_relative(1);
    if (!cmd)
        return NULL;
    return dup_all_args(cmd);
}

