/*
 * Interactive line editor interface.
 *
 * `line_edit` displays PROMPT and allows the user to edit the input using
 * arrow keys, history search and other familiar shortcuts.  The completed
 * command line is returned as a newly allocated string, which the caller
 * must free when no longer needed.
 */
#ifndef LINEEDIT_H
#define LINEEDIT_H

char *line_edit(const char *prompt);

#endif /* LINEEDIT_H */
