/*
 * vush - a simple UNIX shell
 * Licensed under the BSD 2-Clause Simplified License.
 * Interactive line editor.
 */

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

enum lineedit_mode {
    LINEEDIT_EMACS,
    LINEEDIT_VI
};

extern enum lineedit_mode lineedit_mode;

char *line_edit(const char *prompt);

#endif /* LINEEDIT_H */
