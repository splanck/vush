#ifndef DIRSTACK_H
#define DIRSTACK_H

void dirstack_push(const char *dir);
char *dirstack_pop(void);
void dirstack_print(void);

#endif /* DIRSTACK_H */
