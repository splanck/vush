/*
 * vush - a simple UNIX shell
 * Licensed under the BSD 2-Clause Simplified License.
 * Command substitution utilities.
 */

#ifndef CMD_SUBST_H
#define CMD_SUBST_H

char *command_output(const char *cmd);
char *parse_substitution(char **p);

#endif /* CMD_SUBST_H */

