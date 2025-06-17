/*
 * Simple arithmetic expression evaluator used by the shell.
 *
 * Expressions are parsed using a tiny recursive–descent parser with the
 * following grammar (roughly):
 *   expr    := assign
 *   assign  := NAME '=' assign | cmp
 *   cmp     := add ( (== | != | >= | <= | > | <) add )*
 *   add     := mul ( ('+' | '-') mul )*
 *   mul     := unary ( ('*' | '/' | '%') unary )*
 *   unary   := ('+' | '-') unary | primary
 *   primary := NUMBER | NAME | '(' expr ')'
 *
 * Each parse_* function consumes characters from the input string via a
 * char pointer passed by reference.  Variable lookups use get_shell_var()
 * and environment variables and assignments update shell variables via
 * set_shell_var().  eval_arith() simply calls parse_expr on the supplied
 * string and returns the resulting numeric value.
 */
#include "vars.h" // for set_shell_var and get_shell_var
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <errno.h>

/*
 * Advance *s past any whitespace characters.
 * No return value; *s is updated in place.
 */
static void skip_ws(const char **s) {
    while (isspace((unsigned char)**s)) (*s)++;
}

static long long parse_expr(const char **s);
static int parse_error;

/*
 * Parse a primary expression: number, variable or parenthesised subexpression.
 * Returns the parsed value and advances *s past the token.
 */
static long long parse_primary(const char **s) {
    skip_ws(s);
    if (**s == '(') {
        (*s)++; /* '(' */
        long long v = parse_expr(s);
        skip_ws(s);
        if (**s == ')')
            (*s)++;
        else
            parse_error = 1;
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
        if (val) {
            errno = 0;
            long long n = strtoll(val, NULL, 10);
            if (errno == ERANGE)
                parse_error = 1;
            return n;
        }
        return 0;
    }
    const char *p = *s;
    char *end;
    errno = 0;
    long long base = strtoll(p, &end, 10);
    if (errno == ERANGE)
        parse_error = 1;
    if (end > p && *end == '#') {
        if (base >= 2 && base <= 36) {
            p = end + 1;
            errno = 0;
            long long val = strtoll(p, &end, (int)base);
            if (end == p || errno == ERANGE)
                parse_error = 1;
            *s = end;
            return val;
        } else {
            parse_error = 1;
        }
    }
    errno = 0;
    long long v = strtoll(p, &end, 10);
    if (end == p || errno == ERANGE)
        parse_error = 1;
    *s = end;
    return v;
}

/*
 * Parse unary plus/minus or a primary expression.
 * Returns the resulting value; *s is advanced.
 */
static long long parse_unary(const char **s) {
    skip_ws(s);
    if (**s == '+' || **s == '-') {
        char op = *(*s)++;
        long long v = parse_unary(s);
        if (op == '-') {
            if (v == LLONG_MIN) {
                parse_error = 1;
                return 0;
            }
            return -v;
        }
        return v;
    }
    return parse_primary(s);
}

/*
 * Parse multiplicative operators (*, /, %).
 * Returns the computed value; *s is advanced past the expression.
 */
static int mul_overflow(long long a, long long b, long long *out) {
    if (a > 0) {
        if (b > 0) {
            if (a > LLONG_MAX / b) return 1;
        } else if (b < LLONG_MIN / a) {
            return 1;
        }
    } else {
        if (b > 0) {
            if (a < LLONG_MIN / b) return 1;
        } else if (a != 0 && b < LLONG_MAX / a) {
            return 1;
        }
    }
    *out = a * b;
    return 0;
}

static int add_overflow(long long a, long long b, long long *out) {
    if ((b > 0 && a > LLONG_MAX - b) || (b < 0 && a < LLONG_MIN - b))
        return 1;
    *out = a + b;
    return 0;
}

static long long parse_mul(const char **s) {
    long long v = parse_unary(s);
    while (1) {
        skip_ws(s);
        char op = **s;
        if (op == '*' || op == '/' || op == '%') {
            (*s)++;
            long long rhs = parse_unary(s);
            if (op == '*') {
                long long res;
                if (mul_overflow(v, rhs, &res)) {
                    parse_error = 1;
                    return 0;
                }
                v = res;
            } else if (op == '/') {
                if (rhs == 0) {
                    parse_error = 1;
                    return 0;
                }
                if (v == LLONG_MIN && rhs == -1) {
                    parse_error = 1;
                    return 0;
                }
                v /= rhs;
            } else {
                if (rhs == 0) {
                    parse_error = 1;
                    return 0;
                }
                if (v == LLONG_MIN && rhs == -1) {
                    parse_error = 1;
                    return 0;
                }
                v %= rhs;
            }
        } else break;
    }
    return v;
}

/*
 * Parse addition and subtraction operations.
 * Returns the computed value while advancing *s.
 */
static long long parse_add(const char **s) {
    long long v = parse_mul(s);
    while (1) {
        skip_ws(s);
        char op = **s;
        if (op == '+' || op == '-') {
            (*s)++;
            long long rhs = parse_mul(s);
            long long res;
            if (op == '+') {
                if (add_overflow(v, rhs, &res)) {
                    parse_error = 1;
                    return 0;
                }
            } else {
                if (add_overflow(v, -rhs, &res)) {
                    parse_error = 1;
                    return 0;
                }
            }
            v = res;
        } else break;
    }
    return v;
}

/*
 * Parse comparison operators and return 1 or 0.
 * Advances *s past the comparison expression.
 */
static long long parse_cmp(const char **s) {
    long long v = parse_add(s);
    while (1) {
        skip_ws(s);
        if (strncmp(*s, "==", 2) == 0) { *s += 2; long long r = parse_add(s); v = (v == r); }
        else if (strncmp(*s, "!=", 2) == 0) { *s += 2; long long r = parse_add(s); v = (v != r); }
        else if (strncmp(*s, ">=", 2) == 0) { *s += 2; long long r = parse_add(s); v = (v >= r); }
        else if (strncmp(*s, "<=", 2) == 0) { *s += 2; long long r = parse_add(s); v = (v <= r); }
        else if (**s == '>' ) { (*s)++; long long r = parse_add(s); v = (v > r); }
        else if (**s == '<' ) { (*s)++; long long r = parse_add(s); v = (v < r); }
        else break;
    }
    return v;
}

/*
 * Parse assignments of the form NAME=expr.
 * Side effect: updates shell variables via set_shell_var().
 * Returns the assigned or computed value and advances *s.
 */
static long long parse_assign(const char **s) {
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
            long long val = parse_assign(s);
            char buf[32];
            snprintf(buf, sizeof(buf), "%lld", val);
            set_shell_var(name, buf);
            return val;
        }
    }
    *s = save;
    return parse_cmp(s);
}

/* Wrapper for the highest precedence expression parser. */
static long long parse_expr(const char **s) {
    return parse_assign(s);
}

/*
 * Evaluate an arithmetic expression contained in 'expr'.
 * Returns the resulting long long value; does not modify 'expr'.
 */
long long eval_arith(const char *expr, int *err) {
    const char *p = expr;
    parse_error = 0;
    long long v = parse_expr(&p);
    /* Skip any whitespace the parser left behind. */
    skip_ws(&p);
    /* Historically some callers might include carriage returns
     * or stray newlines at the end of the expression.  These
     * should be ignored rather than treated as trailing garbage. */
    while (*p == '\r' || *p == '\n') {
        p++;
        skip_ws(&p);
    }
    if (*p != '\0')
        parse_error = 1;
    if (err)
        *err = parse_error;
    if (parse_error)
        return 0;
    return v;
}
