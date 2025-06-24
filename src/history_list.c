/*
 * vush - a simple UNIX shell
 * Licensed under the BSD 2-Clause Simplified License.
 * In-memory command history list.
 */

/*
 * Command history management routines.
 *
 * History is kept in a list of ``HistEntry`` nodes.  New
 * commands are appended to the end and each entry is assigned an incrementing
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
#include "util.h"
#include "error.h"
#include "list.h"

typedef struct HistEntry {
    int id;
    char cmd[MAX_LINE];
    ListNode node;
} HistEntry;

static List history;

static inline HistEntry *entry_next(HistEntry *e) {
    return e->node.next ? LIST_ENTRY(e->node.next, HistEntry, node) : NULL;
}

static inline HistEntry *entry_prev(HistEntry *e) {
    return e->node.prev ? LIST_ENTRY(e->node.prev, HistEntry, node) : NULL;
}

static inline HistEntry *history_head(void) {
    return history.head ? LIST_ENTRY(history.head, HistEntry, node) : NULL;
}

static inline HistEntry *history_tail(void) {
    return history.tail ? LIST_ENTRY(history.tail, HistEntry, node) : NULL;
}
static HistEntry *cursor = NULL;
static HistEntry *search_cursor = NULL;
static int next_id = 1;
static int skip_next = 0;
static int history_size = 0;
static int max_history = MAX_HISTORY;
static int max_file_history = MAX_HISTORY;

/* Renumber history entries sequentially starting at 1. */
void history_renumber(void)
{
    int id = 1;
    for (ListNode *n = history.head; n; n = n->next) {
        HistEntry *e = LIST_ENTRY(n, HistEntry, node);
        e->id = id++;
    }
    next_id = id;
}

/*
 * File persistence helpers provided by history_file.c
 */
void history_file_append(const char *cmd);
void history_file_rewrite(void);
void history_file_clear(void);

/*
 * Initialise history settings from environment variables.  This function is
 * idempotent and may be called multiple times.
 */
static void history_init(void) {
    static int inited = 0;
    if (inited)
        return;
    list_init(&history);
    const char *env = getenv("VUSH_HISTSIZE");
    if (!env)
        env = getenv("HISTSIZE");
    if (env) {
        long val = strtol(env, NULL, 10);
        if (val > 0)
            max_history = (int)val;
    }
    const char *fenv = getenv("VUSH_HISTFILESIZE");
    if (!fenv)
        fenv = getenv("HISTFILESIZE");
    if (fenv) {
        long val = strtol(fenv, NULL, 10);
        if (val > 0)
            max_file_history = (int)val;
    } else {
        max_file_history = max_history;
    }
    inited = 1;
}

/*
 * Internal helper to append an entry to the history list.  When
 * ``save_file`` is non-zero the entry is also appended to the history file.
 */
/* Add a history entry. When SAVE_FILE is non-zero the entry is also
 * persisted via history_file_append(). */
void history_add_entry(const char *cmd, int save_file) {
    history_init();
    HistEntry *e = malloc(sizeof(HistEntry));
    RETURN_IF_ERR_RET(!e, "malloc", );
    e->id = next_id++;
    strncpy(e->cmd, cmd, MAX_LINE - 1);
    e->cmd[MAX_LINE - 1] = '\0';
    e->node.next = NULL;
    list_append(&history, &e->node);
    history_size++;

    if (history_size > max_history) {
        HistEntry *old = LIST_ENTRY(history.head, HistEntry, node);
        list_remove(&history, &old->node);
        free(old);
        history_size--;
    }

    if (save_file)
        history_file_append(cmd);

    while (history_size > max_file_history) {
        HistEntry *old = LIST_ENTRY(history.head, HistEntry, node);
        list_remove(&history, &old->node);
        free(old);
        history_size--;
    }

    if (save_file && (history_size > max_history || history_size > max_file_history))
        history_file_rewrite();
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
    history_add_entry(cmd, 1);
}

/*
 * Print the entire command history to stdout.
 * Returns nothing.
 */
void print_history(void) {
    history_renumber();
    LIST_FOR_EACH(n, &history) {
        HistEntry *e = LIST_ENTRY(n, HistEntry, node);
        printf("%d %s\n", e->id, e->cmd);
    }
}


/*
 * Step backwards through history and return the previous command.
 * Returns NULL when there is no earlier entry.
 */
const char *history_prev(void) {
    HistEntry *tail_e = history_tail();
    if (!tail_e)
        return NULL;
    if (!cursor)
        cursor = tail_e;
    else if (entry_prev(cursor))
        cursor = entry_prev(cursor);
    return cursor ? cursor->cmd : NULL;
}

/*
 * Move forward through history and return the next command.
 * Returns NULL when there is no later entry.
 */
const char *history_next(void) {
    if (!cursor)
        return NULL;
    if (entry_next(cursor))
        cursor = entry_next(cursor);
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
    if (!term || !*term || !history_tail())
        return NULL;
    HistEntry *start = search_cursor ? entry_prev(search_cursor) : history_tail();
    for (HistEntry *e = start; e; e = entry_prev(e)) {
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
    if (!term || !*term || !history_head())
        return NULL;
    HistEntry *start = search_cursor ? entry_next(search_cursor) : history_head();
    for (HistEntry *e = start; e; e = entry_next(e)) {
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
    ListNode *n = history.head;
    while (n) {
        ListNode *next = n->next;
        HistEntry *e = LIST_ENTRY(n, HistEntry, node);
        free(e);
        n = next;
    }
    list_init(&history);
    cursor = search_cursor = NULL;
    next_id = 1;
    history_size = 0;

    history_file_clear();
}

/*
 * Delete the history entry with the given identifier.  The history file
 * is rewritten to reflect the removal.  Returns nothing.
 */
void delete_history_entry(int id) {
    history_init();
    HistEntry *e = history_head();
    while (e && e->id != id)
        e = entry_next(e);
    if (!e)
        return;

    list_remove(&history, &e->node);

    if (cursor == e)
        cursor = entry_next(e);
    if (search_cursor == e)
        search_cursor = entry_next(e);

    free(e);
    history_size--;

    history_renumber();
    history_file_rewrite();
}

/*
 * Delete the most recent history entry from memory and update the history
 * file.  Has no effect when history is empty.
 */
void delete_last_history_entry(void) {
    HistEntry *t = history_tail();
    if (t)
        delete_history_entry(t->id);
}

/*
 * Return the most recently added command or NULL if history is empty.
 */
const char *history_last(void) {
    HistEntry *t = history_tail();
    return t ? t->cmd : NULL;
}

/*
 * Search from the most recent entry backwards for one starting with 'prefix'.
 * Returns the matched command or NULL if none is found.
 */
const char *history_find_prefix(const char *prefix) {
    if (!prefix || !*prefix)
        return NULL;
    size_t len = strlen(prefix);
    for (HistEntry *e = history_tail(); e; e = entry_prev(e)) {
        if (strncmp(e->cmd, prefix, len) == 0)
            return e->cmd;
    }
    return NULL;
}

/*
 * Retrieve the command with identifier ID or NULL if no such entry exists.
 */
const char *history_get_by_id(int id) {
    for (HistEntry *e = history_head(); e; e = entry_next(e)) {
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
    HistEntry *e = history_tail();
    while (e && --offset > 0)
        e = entry_prev(e);
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
    CHECK_ALLOC_RET(res, NULL);
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

/* Iterate over all history entries, invoking CB for each command. */
void history_list_iter(void (*cb)(const char *cmd, void *arg), void *arg) {
    LIST_FOR_EACH(n, &history) {
        HistEntry *e = LIST_ENTRY(n, HistEntry, node);
        cb(e->cmd, arg);
    }
}

