#ifndef STATE_PATHS_H
#define STATE_PATHS_H

/*
 * Helper functions returning paths to persistent shell state files.
 * Each function returns a newly allocated string or NULL when the
 * location cannot be determined.  Caller must free the returned string.
 */
char *get_alias_file(void);
char *get_func_file(void);
char *get_history_file(void);

#endif /* STATE_PATHS_H */
