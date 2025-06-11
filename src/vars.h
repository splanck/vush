#ifndef VARS_H
#define VARS_H

const char *get_shell_var(const char *name);
char **get_shell_array(const char *name, int *len);
void set_shell_var(const char *name, const char *value);
void set_shell_array(const char *name, char **values, int count);
void unset_shell_var(const char *name);
void free_shell_vars(void);
void push_local_scope(void);
void pop_local_scope(void);
void add_readonly(const char *name);
void record_local_var(const char *name);

#endif /* VARS_H */
