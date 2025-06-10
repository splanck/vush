#ifndef HISTORY_SEARCH_H
#define HISTORY_SEARCH_H

/*
 * Incremental history search routines for the interactive line editor.
 * These declarations allow the editor to search backward or forward
 * through the command history as the user types.
 */

/*
 * Perform an incremental reverse search triggered by Ctrl-R.
 * prompt: prompt prefix displayed to the user.
 * buf: line buffer updated with the result.
 * lenp/posp/disp_lenp: pointers to the current length, cursor position
 *   and displayed length which are updated in place.
 * Returns 1 when an entry is accepted, 0 if cancelled, -1 on error.
 */
int reverse_search(const char *prompt, char *buf, int *lenp, int *posp,
                   int *disp_lenp);

/*
 * Perform an incremental forward search triggered by Ctrl-S.
 * Arguments and return value are the same as reverse_search.
 */
int forward_search(const char *prompt, char *buf, int *lenp, int *posp,
                   int *disp_lenp);

/*
 * Dispatch to reverse_search or forward_search based on the control
 * character received.  Returns the value from the called search
 * function, or 0 when no search is started.
 */
int handle_history_search(char c, const char *prompt, char *buf,
                          int *lenp, int *posp, int *disp_lenp);

#endif /* HISTORY_SEARCH_H */
