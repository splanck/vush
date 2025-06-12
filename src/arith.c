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

/*
 * Advance *s past any whitespace characters.
 * No return value; *s is updated in place.
 */
static void skip_ws(const char **s) {
    while (isspace((unsigned char)**s)) (*s)++;
}

static long parse_expr(const char **s);

/*
 * Parse a primary expression: number, variable or parenthesised subexpression.
 * Returns the parsed value and advances *s past the token.
 */
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
    const char *p = *s;
    char *end;
    long base = strtol(p, &end, 10);
    if (end > p && *end == '#') {
        if (base >= 2 && base <= 36) {
            p = end + 1;
            long val = strtol(p, &end, (int)base);
            *s = end;
            return val;
        }
    }
    long v = strtol(p, &end, 10);
    *s = end;
    return v;
}

/*
 * Parse unary plus/minus or a primary expression.
 * Returns the resulting value; *s is advanced.
 */
static long parse_unary(const char **s) {
    skip_ws(s);
    if (**s == '+' || **s == '-') {
        char op = *(*s)++;
        long v = parse_unary(s);
        return op == '-' ? -v : v;
    }
    return parse_primary(s);
}

/*
 * Parse multiplicative operators (*, /, %).
 * Returns the computed value; *s is advanced past the expression.
 */
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

/*
 * Parse addition and subtraction operations.
 * Returns the computed value while advancing *s.
 */
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

/*
 * Parse comparison operators and return 1 or 0.
 * Advances *s past the comparison expression.
 */
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

/*
 * Parse assignments of the form NAME=expr.
 * Side effect: updates shell variables via set_shell_var().
 * Returns the assigned or computed value and advances *s.
 */
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

/* Wrapper for the highest precedence expression parser. */
static long parse_expr(const char **s) {
    return parse_assign(s);
}

/*
 * Evaluate an arithmetic expression contained in 'expr'.
 * Returns the resulting long value; does not modify 'expr'.
 */
long eval_arith(const char *expr) {
    const char *p = expr;
    long v = parse_expr(&p);
    return v;
}
