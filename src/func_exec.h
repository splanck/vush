#ifndef FUNC_EXEC_H
#define FUNC_EXEC_H
#include "parser.h"

extern int func_return;
int run_function(Command *body, char **args);

#endif /* FUNC_EXEC_H */
