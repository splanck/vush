/*
 * Simple arithmetic expression evaluator used by the shell.
 *
 * Expressions are parsed using a tiny recursive–descent parser with the
 * following grammar.  Each non‑terminal corresponds to a static parse_*()
 * function below.
 *
 *   expression  := assignment
 *   assignment  := NAME '=' assignment |
 *                  NAME '++' | NAME '--' | '++' NAME | '--' NAME |
 *                  logical_or
 *   logical_or  := logical_and ( '||' logical_and )*
 *   logical_and := bit_or ( '&&' bit_or )*
 *   bit_or      := bit_xor ( '|' bit_xor )*
 *   bit_xor     := bit_and ( '^' bit_and )*
 *   bit_and     := equality ( '&' equality )*
 *   equality    := shift ( (== | != | >= | <= | > | <) shift )*
 *   shift       := sum ( ('<<' | '>>') sum )*
 *   sum         := term ( ('+' | '-') term )*
 *   term        := unary ( ('*' | '/' | '%') unary )*
 *   unary       := ('+' | '-' | '!' | '~') unary | factor
 *   factor      := NUMBER | NAME | '(' expression ')'
 *
 * Each parse_* function consumes characters from the input string via a
 * char pointer passed by reference.  Variable lookups use get_shell_var()
 * and environment variables and assignments update shell variables via
* set_shell_var().  eval_arith() simply calls parse_expression on the supplied
 * string and returns the resulting numeric value.
 */
#include "vars.h" // for set_shell_var and get_shell_var
#include "arith.h"
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

static long long parse_expression(ArithState *state);

/*
 * Parse a factor: number, variable or parenthesised subexpression.
 * Returns the parsed value and advances *s past the token.
 */
static long long parse_factor(ArithState *state) {
    if (state->err) return 0;
    skip_ws(&state->p);
    if (*state->p == '(') {
        state->p++; /* '(' */
        long long value = parse_expression(state);
        if (state->err) return 0;
        skip_ws(&state->p);
        if (*state->p == ')')
            state->p++;
        else
            state->err = 1;
        return value;
    }
    if (isalpha((unsigned char)*state->p) || *state->p == '_') {
        char name[64]; int len = 0;
        while (isalnum((unsigned char)*state->p) || *state->p == '_') {
            if (len < (int)sizeof(name) - 1)
                name[len++] = *state->p;
            state->p++;
        }
        name[len] = '\0';
        const char *val = get_shell_var(name);
        if (!val) val = getenv(name);
        if (val) {
            errno = 0;
            long long num = strtoll(val, NULL, 10);
            if (errno == ERANGE)
                state->err = 1;
            return num;
        }
        return 0;
    }
    const char *p = state->p;
    char *end;
    errno = 0;
    long long base = strtoll(p, &end, 10);
    if (errno == ERANGE)
        state->err = 1;
    if (end > p && *end == '#') {
        if (base >= 2 && base <= 36) {
            p = end + 1;
            errno = 0;
            long long val = strtoll(p, &end, (int)base);
            if (end == p || errno == ERANGE)
                state->err = 1;
            state->p = end;
            return val;
        } else {
            state->err = 1;
        }
    }
    errno = 0;
    long long value = strtoll(p, &end, 10);
    if (end == p || errno == ERANGE)
        state->err = 1;
    state->p = end;
    return value;
}

/*
 * Parse unary plus/minus or a factor.
 * Returns the resulting value; *s is advanced.
 */
static long long parse_unary(ArithState *state) {
    if (state->err) return 0;
    skip_ws(&state->p);
    if (*state->p == '+' || *state->p == '-' || *state->p == '!' || *state->p == '~') {
        char op = *state->p++;
        long long operand = parse_unary(state);
        if (state->err) return 0;
        switch (op) {
            case '-':
                if (operand == LLONG_MIN) {
                    state->err = 1;
                    return 0;
                }
                return -operand;
            case '!':
                return !operand;
            case '~':
                return ~operand;
            default:
                return operand; /* unary plus */
        }
    }
    return parse_factor(state);
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

static long long parse_term(ArithState *state) {
    if (state->err) return 0;
    long long value = parse_unary(state);
    if (state->err) return 0;
    while (1) {
        skip_ws(&state->p);
        char op = *state->p;
        if (op == '*' || op == '/' || op == '%') {
            state->p++;
            long long rhs = parse_unary(state);
            if (state->err) return 0;
            if (op == '*') {
                long long result;
                if (mul_overflow(value, rhs, &result)) {
                    state->err = 1;
                    return 0;
                }
                value = result;
            } else if (op == '/') {
                if (rhs == 0) {
                    state->err = 1;
                    return 0;
                }
                if (value == LLONG_MIN && rhs == -1) {
                    state->err = 1;
                    return 0;
                }
                value /= rhs;
            } else {
                if (rhs == 0) {
                    state->err = 1;
                    return 0;
                }
                if (value == LLONG_MIN && rhs == -1) {
                    state->err = 1;
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
static long long parse_sum(ArithState *state) {
    if (state->err) return 0;
    long long value = parse_term(state);
    if (state->err) return 0;
    while (1) {
        skip_ws(&state->p);
        char op = *state->p;
        if (op == '+' || op == '-') {
            state->p++;
            long long rhs = parse_term(state);
            if (state->err) return 0;
            long long result;
            if (op == '+') {
                if (add_overflow(value, rhs, &result)) {
                    state->err = 1;
                    return 0;
                }
            } else {
                if (add_overflow(value, -rhs, &result)) {
                    state->err = 1;
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
static long long parse_shift(ArithState *state) {
    if (state->err) return 0;
    long long value = parse_sum(state);
    if (state->err) return 0;
    while (1) {
        skip_ws(&state->p);
        if (strncmp(state->p, "<<", 2) == 0) {
            state->p += 2;
            long long rhs = parse_sum(state);
            if (state->err) return 0;
            if (rhs < 0 || rhs >= (long long)(sizeof(long long) * 8)) {
                state->err = 1;
                return 0;
            }
            value <<= rhs;
        } else if (strncmp(state->p, ">>", 2) == 0) {
            state->p += 2;
            long long rhs = parse_sum(state);
            if (state->err) return 0;
            if (rhs < 0 || rhs >= (long long)(sizeof(long long) * 8)) {
                state->err = 1;
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
static long long parse_equality(ArithState *state) {
    if (state->err) return 0;
    long long value = parse_shift(state);
    if (state->err) return 0;
    while (1) {
        skip_ws(&state->p);
        if (strncmp(state->p, "==", 2) == 0) {
            state->p += 2; long long rhs = parse_shift(state); if (state->err) return 0; value = (value == rhs);
        }
        else if (strncmp(state->p, "!=", 2) == 0) {
            state->p += 2; long long rhs = parse_shift(state); if (state->err) return 0; value = (value != rhs);
        }
        else if (strncmp(state->p, ">=", 2) == 0) {
            state->p += 2; long long rhs = parse_shift(state); if (state->err) return 0; value = (value >= rhs);
        }
        else if (strncmp(state->p, "<=", 2) == 0) {
            state->p += 2; long long rhs = parse_shift(state); if (state->err) return 0; value = (value <= rhs);
        }
        else if (*state->p == '>') {
            state->p++; long long rhs = parse_shift(state); if (state->err) return 0; value = (value > rhs);
        }
        else if (*state->p == '<') {
            state->p++; long long rhs = parse_shift(state); if (state->err) return 0; value = (value < rhs);
        }
        else break;
    }
    return value;
}

/*
 * Parse bitwise AND operations.
 */
static long long parse_bit_and(ArithState *state) {
    if (state->err) return 0;
    long long value = parse_equality(state);
    if (state->err) return 0;
    while (1) {
        skip_ws(&state->p);
        if (*state->p == '&') {
            state->p++;
            long long rhs = parse_equality(state);
            if (state->err) return 0;
            value &= rhs;
        } else break;
    }
    return value;
}

/*
 * Parse bitwise XOR operations.
 */
static long long parse_bit_xor(ArithState *state) {
    if (state->err) return 0;
    long long value = parse_bit_and(state);
    if (state->err) return 0;
    while (1) {
        skip_ws(&state->p);
        if (*state->p == '^') {
            state->p++;
            long long rhs = parse_bit_and(state);
            if (state->err) return 0;
            value ^= rhs;
        } else break;
    }
    return value;
}

/*
 * Parse bitwise OR operations.
 */
static long long parse_bit_or(ArithState *state) {
    if (state->err) return 0;
    long long value = parse_bit_xor(state);
    if (state->err) return 0;
    while (1) {
        skip_ws(&state->p);
        if (*state->p == '|') {
            state->p++;
            long long rhs = parse_bit_xor(state);
            if (state->err) return 0;
            value |= rhs;
        } else break;
    }
    return value;
}

/*
 * Parse logical AND operations.
 */
static long long parse_logical_and(ArithState *state) {
    if (state->err) return 0;
    long long value = parse_bit_or(state);
    if (state->err) return 0;
    while (1) {
        skip_ws(&state->p);
        if (strncmp(state->p, "&&", 2) == 0) {
            state->p += 2;
            long long rhs = parse_bit_or(state);
            if (state->err) return 0;
            value = value && rhs;
        } else break;
    }
    return value;
}

/*
 * Parse logical OR operations.
 */
static long long parse_logical_or(ArithState *state) {
    if (state->err) return 0;
    long long value = parse_logical_and(state);
    if (state->err) return 0;
    while (1) {
        skip_ws(&state->p);
        if (strncmp(state->p, "||", 2) == 0) {
            state->p += 2;
            long long rhs = parse_logical_and(state);
            if (state->err) return 0;
            value = value || rhs;
        } else break;
    }
    return value;
}

/*
 * Parse assignments of the form NAME=expr.
 * Side effect: updates shell variables via set_shell_var().
 * Returns the assigned or computed value and advances *s.
 */
static long long parse_assignment(ArithState *state) {
    if (state->err) return 0;
    skip_ws(&state->p);
    const char *save = state->p;
    int prefix = 0; char incop = 0;
    if (strncmp(state->p, "++", 2) == 0 || strncmp(state->p, "--", 2) == 0) {
        prefix = 1; incop = state->p[0];
        state->p += 2;
        skip_ws(&state->p);
    }

    if ((isalpha((unsigned char)*state->p) || *state->p == '_')) {
        char name[64]; int len = 0;
        while (isalnum((unsigned char)*state->p) || *state->p == '_') {
            if (len < (int)sizeof(name) - 1)
                name[len++] = *state->p;
            state->p++;
        }
        name[len] = '\0';
        skip_ws(&state->p);
        if (*state->p == '=') {
            state->p++;
            long long value = parse_assignment(state);
            if (state->err) return 0;
            char buf[32];
            snprintf(buf, sizeof(buf), "%lld", value);
            set_shell_var(name, buf);
            return value;
        }

        /* Retrieve current variable value */
        const char *val = get_shell_var(name);
        if (!val) val = getenv(name);
        errno = 0;
        long long cur = val ? strtoll(val, NULL, 10) : 0;
        if (errno == ERANGE) state->err = 1;

        if (prefix) {
            long long newv;
            if (add_overflow(cur, (incop == '+') ? 1 : -1, &newv)) {
                state->err = 1;
                return 0;
            }
            char buf[32];
            snprintf(buf, sizeof(buf), "%lld", newv);
            set_shell_var(name, buf);
            return newv;
        }

        if (strncmp(state->p, "++", 2) == 0 || strncmp(state->p, "--", 2) == 0) {
            incop = state->p[0];
            state->p += 2;
            long long newv;
            if (add_overflow(cur, (incop == '+') ? 1 : -1, &newv)) {
                state->err = 1;
                return 0;
            }
            char buf[32];
            snprintf(buf, sizeof(buf), "%lld", newv);
            set_shell_var(name, buf);
            return cur; /* postfix returns old value */
        }
    }
    state->p = save;
    return parse_logical_or(state);
}

/* Wrapper for the top-level expression parser. */
static long long parse_expression(ArithState *state) {
    return parse_assignment(state);
}

/*
 * Evaluate an arithmetic expression contained in 'expr'.
 * Returns the resulting long long value; does not modify 'expr'.
 */
long long eval_arith(const char *expr, int *err) {
    ArithState st = { .p = expr, .err = 0 };
    long long result = parse_expression(&st);
    /* Skip any whitespace the parser left behind. */
    const char *p = st.p;
    skip_ws(&p);
    /* Historically some callers might include carriage returns
     * or stray newlines at the end of the expression.  These
     * should be ignored rather than treated as trailing garbage. */
    while (*p == '\r' || *p == '\n') {
        p++;
        skip_ws(&p);
    }
    if (*p != '\0')
        st.err = 1;
    if (err)
        *err = st.err;
    if (st.err)
        return 0;
    return result;
}
