/*
 * Directory stack API.
 * Exposes a simple push/pop interface for maintaining the shell's directory stack.
 */

#ifndef DIRSTACK_H
#define DIRSTACK_H

void dirstack_push(const char *dir);
char *dirstack_pop(void);
void dirstack_print(void);
void dirstack_clear(void);
void update_pwd_env(const char *oldpwd);

#endif /* DIRSTACK_H */
