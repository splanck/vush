#ifndef HISTORY_H
#define HISTORY_H

void add_history(const char *cmd);
void print_history(void);
void load_history(void);
const char *history_prev(void);
const char *history_next(void);
void history_reset_cursor(void);
void clear_history(void);

#endif /* HISTORY_H */
