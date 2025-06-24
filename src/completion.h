/*
 * vush - a simple UNIX shell
 * Licensed under the BSD 2-Clause Simplified License.
 * Line editor completion support.
 */

/*
 * Command completion helpers for the line editor.
 * Builtin names and files are searched to complete the word at the cursor.
 */
#ifndef COMPLETION_H
#define COMPLETION_H
/*
 * handle_completion() searches for completions.
 *  prompt     - current prompt string used for redraws
 *  buf        - input buffer being edited
 *  lenp       - pointer to length of buf
 *  posp       - pointer to cursor position
 *  disp_lenp  - pointer to displayed length
 * The function inserts the chosen completion into buf and updates the pointers.
 */
void handle_completion(const char *prompt, char *buf, int *lenp, int *posp,
                       int *disp_lenp);

#endif /* COMPLETION_H */
