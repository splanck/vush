
/*
 * Shell function execution helpers.
 * Exposes helpers for executing parsed shell functions.
 */
#ifndef FUNC_EXEC_H
#define FUNC_EXEC_H
#include "parser.h"
#include "builtins.h"

extern int func_return;
/* Execute parsed function BODY with argument vector ARGS and return its exit status. */
int run_function(FuncEntry *fn, char **args);

#endif /* FUNC_EXEC_H */
