/*
 * Simple arithmetic expression evaluator used by the shell.
 *
 * Expressions are parsed using a tiny recursive–descent parser with the
 * following grammar.  Each non‑terminal corresponds to a static parse_*()
 * function below.
 *
 *   expression  := assignment
 *   assignment  := NAME '=' assignment | bit_or
 *   bit_or      := bit_xor ( '|' bit_xor )*
 *   bit_xor     := bit_and ( '^' bit_and )*
 *   bit_and     := equality ( '&' equality )*
 *   equality    := shift ( (== | != | >= | <= | > | <) shift )*
 *   shift       := sum ( ('<<' | '>>') sum )*
 *   sum         := term ( ('+' | '-') term )*
 *   term        := unary ( ('*' | '/' | '%') unary )*
 *   unary       := ('+' | '-') unary | factor
 *   factor      := NUMBER | NAME | '(' expression ')'
 *
 * Each parse_* function consumes characters from the input string via a
 * char pointer passed by reference.  Variable lookups use get_shell_var()
 * and environment variables and assignments update shell variables via
* set_shell_var().  eval_arith() simply calls parse_expression on the supplied
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

static long long parse_expression(const char **s);
static int parse_error;

/*
 * Parse a factor: number, variable or parenthesised subexpression.
 * Returns the parsed value and advances *s past the token.
 */
static long long parse_factor(const char **s) {
    skip_ws(s);
    if (**s == '(') {
        (*s)++; /* '(' */
        long long value = parse_expression(s);
        skip_ws(s);
        if (**s == ')')
            (*s)++;
        else
            parse_error = 1;
        return value;
    }
    if (isalpha((unsigned char)**s) || **s == '_') {
        char name[64]; int len = 0;
        while (isalnum((unsigned char)**s) || **s == '_') {
            if (len < (int)sizeof(name) - 1)
                name[len++] = **s;
            (*s)++;
        }
        name[len] = '\0';
        skip_ws(s);
        long long idx = -1;
        if (**s == '[') {
            (*s)++; /* '[' */
            idx = parse_expression(s);
            skip_ws(s);
            if (**s == ']')
                (*s)++;
            else
                parse_error = 1;
            skip_ws(s);
        }
        const char *val = NULL;
        if (idx >= 0) {
            int alen = 0; char **arr = get_shell_array(name, &alen);
            if (arr && idx >= 0 && idx < alen)
                val = arr[idx];
        } else {
            val = get_shell_var(name);
        }
        if (!val)
            val = getenv(name);
        if (val) {
            errno = 0;
            long long num = strtoll(val, NULL, 10);
            if (errno == ERANGE)
                parse_error = 1;
            return num;
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
    long long value = strtoll(p, &end, 10);
    if (end == p || errno == ERANGE)
        parse_error = 1;
    *s = end;
    return value;
}

/*
 * Parse unary plus/minus or a factor.
 * Returns the resulting value; *s is advanced.
 */
static long long parse_unary(const char **s) {
    skip_ws(s);
    if (**s == '+' || **s == '-') {
        char op = *(*s)++;
        long long operand = parse_unary(s);
        if (op == '-') {
            if (operand == LLONG_MIN) {
                parse_error = 1;
                return 0;
            }
            return -operand;
        }
        return operand;
    }
    return parse_factor(s);
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

static long long parse_term(const char **s) {
    long long value = parse_unary(s);
    while (1) {
        skip_ws(s);
        char op = **s;
        if (op == '*' || op == '/' || op == '%') {
            (*s)++;
            long long rhs = parse_unary(s);
            if (op == '*') {
                long long result;
                if (mul_overflow(value, rhs, &result)) {
                    parse_error = 1;
                    return 0;
                }
                value = result;
            } else if (op == '/') {
                if (rhs == 0) {
                    parse_error = 1;
                    return 0;
                }
                if (value == LLONG_MIN && rhs == -1) {
                    parse_error = 1;
                    return 0;
                }
                value /= rhs;
            } else {
                if (rhs == 0) {
                    parse_error = 1;
                    return 0;
                }
                if (value == LLONG_MIN && rhs == -1) {
                    parse_error = 1;
                    return 0;
                }
                value %= rhs;
            }
        } else break;
    }
    return value;
}

/*
 * Parse addition and subtraction operations.
 * Returns the computed value while advancing *s.
 */
static long long parse_sum(const char **s) {
    long long value = parse_term(s);
    while (1) {
        skip_ws(s);
        char op = **s;
        if (op == '+' || op == '-') {
            (*s)++;
            long long rhs = parse_term(s);
            long long result;
            if (op == '+') {
                if (add_overflow(value, rhs, &result)) {
                    parse_error = 1;
                    return 0;
                }
            } else {
                if (add_overflow(value, -rhs, &result)) {
                    parse_error = 1;
                    return 0;
                }
            }
            value = result;
        } else break;
    }
    return value;
}

/*
 * Parse shift operators (<< and >>).
 * Returns the computed value while advancing *s.
 */
static long long parse_shift(const char **s) {
    long long value = parse_sum(s);
    while (1) {
        skip_ws(s);
        if (strncmp(*s, "<<", 2) == 0) {
            *s += 2;
            long long rhs = parse_sum(s);
            if (rhs < 0 || rhs >= (long long)(sizeof(long long) * 8)) {
                parse_error = 1;
                return 0;
            }
            value <<= rhs;
        } else if (strncmp(*s, ">>", 2) == 0) {
            *s += 2;
            long long rhs = parse_sum(s);
            if (rhs < 0 || rhs >= (long long)(sizeof(long long) * 8)) {
                parse_error = 1;
                return 0;
            }
            value >>= rhs;
        } else break;
    }
    return value;
}

/*
 * Parse equality and relational operators and return 1 or 0.
 * Advances *s past the comparison expression.
 */
static long long parse_equality(const char **s) {
    long long value = parse_shift(s);
    while (1) {
        skip_ws(s);
        if (strncmp(*s, "==", 2) == 0) { *s += 2; long long rhs = parse_shift(s); value = (value == rhs); }
        else if (strncmp(*s, "!=", 2) == 0) { *s += 2; long long rhs = parse_shift(s); value = (value != rhs); }
        else if (strncmp(*s, ">=", 2) == 0) { *s += 2; long long rhs = parse_shift(s); value = (value >= rhs); }
        else if (strncmp(*s, "<=", 2) == 0) { *s += 2; long long rhs = parse_shift(s); value = (value <= rhs); }
        else if (**s == '>' ) { (*s)++; long long rhs = parse_shift(s); value = (value > rhs); }
        else if (**s == '<' ) { (*s)++; long long rhs = parse_shift(s); value = (value < rhs); }
        else break;
    }
    return value;
}

/*
 * Parse bitwise AND operations.
 */
static long long parse_bit_and(const char **s) {
    long long value = parse_equality(s);
    while (1) {
        skip_ws(s);
        if (**s == '&') {
            (*s)++;
            long long rhs = parse_equality(s);
            value &= rhs;
        } else break;
    }
    return value;
}

/*
 * Parse bitwise XOR operations.
 */
static long long parse_bit_xor(const char **s) {
    long long value = parse_bit_and(s);
    while (1) {
        skip_ws(s);
        if (**s == '^') {
            (*s)++;
            long long rhs = parse_bit_and(s);
            value ^= rhs;
        } else break;
    }
    return value;
}

/*
 * Parse bitwise OR operations.
 */
static long long parse_bit_or(const char **s) {
    long long value = parse_bit_xor(s);
    while (1) {
        skip_ws(s);
        if (**s == '|') {
            (*s)++;
            long long rhs = parse_bit_xor(s);
            value |= rhs;
        } else break;
    }
    return value;
}

/*
 * Parse assignments of the form NAME=expr.
 * Side effect: updates shell variables via set_shell_var().
 * Returns the assigned or computed value and advances *s.
 */
static long long parse_assignment(const char **s) {
    skip_ws(s);
    const char *save = *s;
    if ((isalpha((unsigned char)**s) || **s == '_')) {
        char name[64]; int len = 0;
        while (isalnum((unsigned char)**s) || **s == '_') {
            if (len < (int)sizeof(name) - 1)
                name[len++] = **s;
            (*s)++;
        }
        name[len] = '\0';
        skip_ws(s);
        long long idx = -1;
        if (**s == '[') {
            (*s)++;
            idx = parse_expression(s);
            skip_ws(s);
            if (**s == ']')
                (*s)++;
            else
                parse_error = 1;
            skip_ws(s);
        }
        if (**s == '=') {
            (*s)++;
            long long value = parse_assignment(s);
            char buf[32];
            snprintf(buf, sizeof(buf), "%lld", value);
            if (idx >= 0)
                set_shell_array_index(name, (int)idx, buf);
            else
                set_shell_var(name, buf);
            return value;
        }
    }
    *s = save;
    return parse_bit_or(s);
}

/* Wrapper for the top-level expression parser. */
static long long parse_expression(const char **s) {
    return parse_assignment(s);
}

/*
 * Evaluate an arithmetic expression contained in 'expr'.
 * Returns the resulting long long value; does not modify 'expr'.
 */
long long eval_arith(const char *expr, int *err) {
    const char *p = expr;
    parse_error = 0;
    long long result = parse_expression(&p);
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
    return result;
}
