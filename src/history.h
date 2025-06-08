#ifndef HISTORY_H
#define HISTORY_H

#define MAX_HISTORY 1000

void add_history(const char *cmd);
void print_history(void);
void load_history(void);
const char *history_prev(void);
const char *history_next(void);
void history_reset_cursor(void);
const char *history_search_prev(const char *term);
const char *history_search_next(const char *term);
void history_reset_search(void);
void clear_history(void);
void delete_history_entry(int id);

#endif /* HISTORY_H */
