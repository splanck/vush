#ifndef HISTORY_H
#define HISTORY_H

/*
 * Command history interface.
 *
 * The shell stores history entries in a doubly linked list.  Each added
 * command receives an incrementing identifier and is appended to the tail of
 * the list.  Entries are persisted to the file pointed to by $VUSH_HISTFILE
 * (defaulting to $HOME/.vush_history) so the list can be reloaded on startup.
 * Helper cursors are used to walk the list when navigating with the arrow keys
 * or searching through history.
 */

/* Maximum number of entries retained when VUSH_HISTSIZE is unset. */
#define MAX_HISTORY 1000

/* Record CMD in history and append it to the history file. */
void add_history(const char *cmd);

/* Print the entire history list to stdout. */
void print_history(void);

/* Load the history file into memory. */
void load_history(void);

/* Step backwards through history and return the previous command or NULL. */
const char *history_prev(void);

/* Step forwards through history and return the next command or NULL. */
const char *history_next(void);

/* Reset the navigation cursor used by history_prev/history_next. */
void history_reset_cursor(void);

/*
 * Search backward for TERM starting from the last match.  Returns the matched
 * command or NULL if none is found.
 */
const char *history_search_prev(const char *term);

/* Continue searching forward for TERM from the last match and return it or NULL. */
const char *history_search_next(const char *term);

/* Clear the search cursor used by history_search_prev/history_search_next. */
void history_reset_search(void);

/* Remove all history entries and truncate the history file. */
void clear_history(void);

/* Delete the entry with identifier ID from the list and history file. */
void delete_history_entry(int id);

/* Return the most recently added command or NULL if history is empty. */
const char *history_last(void);

/* Find the most recent entry starting with PREFIX or return NULL. */
const char *history_find_prefix(const char *prefix);

/* Retrieve a command by absolute identifier or NULL if not present. */
const char *history_get_by_id(int id);

/* Return the OFFSET-th previous command where 1 is the last entry. */
const char *history_get_relative(int offset);

/* Obtain the last argument of the most recent command. Caller must free. */
char *history_last_word(void);

/* Obtain all arguments from the most recent command. Caller must free. */
char *history_all_words(void);

#endif /* HISTORY_H */
