/* Arithmetic evaluation module */
#include "builtins.h" // for set_shell_var and get_shell_var
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static void skip_ws(const char **s) {
    while (isspace((unsigned char)**s)) (*s)++;
}

static long parse_expr(const char **s);

static long parse_primary(const char **s) {
    skip_ws(s);
    if (**s == '(') {
        (*s)++; /* '(' */
        long v = parse_expr(s);
        skip_ws(s);
        if (**s == ')') (*s)++;
        return v;
    }
    if (isalpha((unsigned char)**s) || **s == '_') {
        char name[64]; int n = 0;
        while (isalnum((unsigned char)**s) || **s == '_') {
            if (n < (int)sizeof(name) - 1)
                name[n++] = **s;
            (*s)++;
        }
        name[n] = '\0';
        const char *val = get_shell_var(name);
        if (!val) val = getenv(name);
        return val ? strtol(val, NULL, 10) : 0;
    }
    char *end;
    long v = strtol(*s, &end, 10);
    *s = end;
    return v;
}

static long parse_unary(const char **s) {
    skip_ws(s);
    if (**s == '+' || **s == '-') {
        char op = *(*s)++;
        long v = parse_unary(s);
        return op == '-' ? -v : v;
    }
    return parse_primary(s);
}

static long parse_mul(const char **s) {
    long v = parse_unary(s);
    while (1) {
        skip_ws(s);
        char op = **s;
        if (op == '*' || op == '/' || op == '%') {
            (*s)++;
            long rhs = parse_unary(s);
            if (op == '*') v *= rhs;
            else if (op == '/') v /= rhs;
            else v %= rhs;
        } else break;
    }
    return v;
}

static long parse_add(const char **s) {
    long v = parse_mul(s);
    while (1) {
        skip_ws(s);
        char op = **s;
        if (op == '+' || op == '-') {
            (*s)++;
            long rhs = parse_mul(s);
            if (op == '+') v += rhs;
            else v -= rhs;
        } else break;
    }
    return v;
}

static long parse_cmp(const char **s) {
    long v = parse_add(s);
    while (1) {
        skip_ws(s);
        if (strncmp(*s, "==", 2) == 0) { *s += 2; long r = parse_add(s); v = (v == r); }
        else if (strncmp(*s, "!=", 2) == 0) { *s += 2; long r = parse_add(s); v = (v != r); }
        else if (strncmp(*s, ">=", 2) == 0) { *s += 2; long r = parse_add(s); v = (v >= r); }
        else if (strncmp(*s, "<=", 2) == 0) { *s += 2; long r = parse_add(s); v = (v <= r); }
        else if (**s == '>' ) { (*s)++; long r = parse_add(s); v = (v > r); }
        else if (**s == '<' ) { (*s)++; long r = parse_add(s); v = (v < r); }
        else break;
    }
    return v;
}

static long parse_assign(const char **s) {
    skip_ws(s);
    const char *save = *s;
    if ((isalpha((unsigned char)**s) || **s == '_')) {
        char name[64]; int n = 0;
        while (isalnum((unsigned char)**s) || **s == '_') {
            if (n < (int)sizeof(name) - 1)
                name[n++] = **s;
            (*s)++;
        }
        name[n] = '\0';
        skip_ws(s);
        if (**s == '=') {
            (*s)++;
            long val = parse_assign(s);
            char buf[32];
            snprintf(buf, sizeof(buf), "%ld", val);
            set_shell_var(name, buf);
            return val;
        }
    }
    *s = save;
    return parse_cmp(s);
}

static long parse_expr(const char **s) {
    return parse_assign(s);
}

long eval_arith(const char *expr) {
    const char *p = expr;
    long v = parse_expr(&p);
    return v;
}
