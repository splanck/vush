/*
 * vush - a simple UNIX shell
 * Licensed under the BSD 2-Clause Simplified License.
 * Shell variable management.
 */

#ifndef VARS_H
#define VARS_H

const char *get_shell_var(const char *name);
char **get_shell_array(const char *name, int *len);
/*
 * Assign a value to a shell variable. If 'value' is NULL it is treated as an
 * empty string.
 */
void set_shell_var(const char *name, const char *value);
void set_shell_array(const char *name, char **values, int count);
void unset_shell_var(const char *name);
void free_shell_vars(void);
/*
 * Push a new local scope for shell variables.
 *
 * Returns 1 on success and 0 if memory allocation fails.
 */
int push_local_scope(void);
void pop_local_scope(void);
void add_readonly(const char *name);
void record_local_var(const char *name);
void print_array(const char *prefix, char **arr, int len);
void print_readonly_vars(void);
void print_shell_vars(void);
int export_var(const char *name, const char *val);
void unset_var(const char *name);

#endif /* VARS_H */
