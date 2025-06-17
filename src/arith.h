#ifndef ARITH_H
#define ARITH_H

typedef struct ArithState {
    const char *p;
    int err;
    char err_msg[64];
} ArithState;

long long eval_arith(const char *expr, int *err);

#endif /* ARITH_H */
