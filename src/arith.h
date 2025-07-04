/*
 * vush - a simple UNIX shell
 * Licensed under the BSD 2-Clause Simplified License.
 * Arithmetic expression evaluation.
 */

#ifndef ARITH_H
#define ARITH_H

typedef struct ArithState {
    const char *p;
    int err;
    char err_msg[64];
} ArithState;

long long eval_arith(const char *expr, int *err, char **errmsg);

#endif /* ARITH_H */
