#define _GNU_SOURCE
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

#define PARSE_RHS(fn)              \
    long long rhs = fn(state);    \
    if (state->err) return 0;

static ArithState *current_state = NULL;

static void arith_set_error(const char *msg) {
    if (current_state && !current_state->err) {
        current_state->err = 1;
        strncpy(current_state->err_msg, msg, sizeof(current_state->err_msg) - 1);
        current_state->err_msg[sizeof(current_state->err_msg) - 1] = '\0';
    }
}

/*
 * Advance *s past any whitespace characters.
 * No return value; *s is updated in place.
 */
static void skip_ws(const char **s) {
    while (isspace((unsigned char)**s)) (*s)++;
}

/*
 * Wrapper around strtoll() that ensures the entire string was consumed.
 * Returns 0 on success and stores the result in *out.  On failure, returns
 * -1 and sets errno to ERANGE for overflow or EINVAL for invalid input.
 */
static int parse_ll(const char *s, long long *out) {
    char *end;
    errno = 0;
    long long val = strtoll(s, &end, 10);
    if (errno == ERANGE) {
        if (out) *out = val;
        return -1;
    }
    if (end == s || *end != '\0') {
        errno = EINVAL;
        if (out) *out = val;
        return -1;
    }
    if (out) *out = val;
    return 0;
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
            arith_set_error("missing ')'");
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
            long long num = 0;
            if (parse_ll(val, &num) < 0) {
                if (errno == ERANGE)
                    arith_set_error("overflow");
                else
                    arith_set_error("invalid number");
            }
            return num;
        }
        return 0;
    }
    const char *p = state->p;
    char *end;
    errno = 0;
    long long base = strtoll(p, &end, 10);
    if (errno == ERANGE)
        arith_set_error("overflow");
    if (end > p && *end == '#') {
        if (base >= 2 && base <= 36) {
            p = end + 1;
            errno = 0;
            long long val = strtoll(p, &end, (int)base);
            if (end == p || errno == ERANGE)
                arith_set_error("invalid number");
            state->p = end;
            return val;
        } else {
            arith_set_error("invalid base");
        }
    }
    errno = 0;
    long long value = strtoll(p, &end, 10);
    if (end == p || errno == ERANGE)
        arith_set_error("invalid number");
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
                    arith_set_error("overflow");
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

static int sub_overflow(long long a, long long b, long long *out) {
    if ((b > 0 && a < LLONG_MIN + b) || (b < 0 && a > LLONG_MAX + b))
        return 1;
    *out = a - b;
    return 0;
}

static int lshift_overflow(long long a, long long b, long long *out) {
    if (b < 0 || b >= (long long)(sizeof(long long) * 8))
        return 1;
    __int128 result = (__int128)a << b;
    if (result > LLONG_MAX || result < LLONG_MIN)
        return 1;
    *out = (long long)result;
    return 0;
}

static int rshift_overflow(long long a, long long b, long long *out) {
    if (b < 0 || b >= (long long)(sizeof(long long) * 8))
        return 1;
    __int128 result = (__int128)a >> b;
    if (result > LLONG_MAX || result < LLONG_MIN)
        return 1;
    *out = (long long)result;
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
                    arith_set_error("overflow");
                    return 0;
                }
                value = result;
            } else if (op == '/') {
                if (rhs == 0) {
                    arith_set_error("divide by zero");
                    return 0;
                }
                if (value == LLONG_MIN && rhs == -1) {
                    arith_set_error("overflow");
                    return 0;
                }
                value /= rhs;
            } else {
                if (rhs == 0) {
                    arith_set_error("divide by zero");
                    return 0;
                }
                if (value == LLONG_MIN && rhs == -1) {
                    arith_set_error("overflow");
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
                    arith_set_error("overflow");
                    return 0;
                }
            } else {
                if (sub_overflow(value, rhs, &result)) {
                    arith_set_error("overflow");
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
                arith_set_error("shift out of range");
                return 0;
            }
            long long result;
            if (lshift_overflow(value, rhs, &result)) {
                arith_set_error("overflow");
                return 0;
            }
            value = result;
        } else if (strncmp(state->p, ">>", 2) == 0) {
            state->p += 2;
            long long rhs = parse_sum(state);
            if (state->err) return 0;
            if (rhs < 0 || rhs >= (long long)(sizeof(long long) * 8)) {
                arith_set_error("shift out of range");
                return 0;
            }
            long long result;
            if (rshift_overflow(value, rhs, &result)) {
                arith_set_error("overflow");
                return 0;
            }
            value = result;
        } else break;
    }
    return value;
}

/*
 * Parse equality and relational operators (==, !=, >=, <=, >, <).
 * Precedence: lower than bitwise AND but higher than shift. Left-associative.
 * Advances *s past the comparison expression and returns 1 or 0.
 */
static long long parse_equality(ArithState *state) {
    if (state->err) return 0;
    long long value = parse_shift(state);
    if (state->err) return 0;
    while (1) {
        skip_ws(&state->p);
        if (strncmp(state->p, "==", 2) == 0) {
            state->p += 2;
            PARSE_RHS(parse_shift);
            value = (value == rhs);
        } else if (strncmp(state->p, "!=", 2) == 0) {
            state->p += 2;
            PARSE_RHS(parse_shift);
            value = (value != rhs);
        } else if (strncmp(state->p, ">=", 2) == 0) {
            state->p += 2;
            PARSE_RHS(parse_shift);
            value = (value >= rhs);
        } else if (strncmp(state->p, "<=", 2) == 0) {
            state->p += 2;
            PARSE_RHS(parse_shift);
            value = (value <= rhs);
        } else if (*state->p == '>') {
            state->p++;
            PARSE_RHS(parse_shift);
            value = (value > rhs);
        } else if (*state->p == '<') {
            state->p++;
            PARSE_RHS(parse_shift);
            value = (value < rhs);
        } else {
            break;
        }
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

        int oplen = 0;
        enum { OP_NONE, OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD,
               OP_SHL, OP_SHR, OP_AND, OP_XOR, OP_OR } op = OP_NONE;
        if (strncmp(state->p, "+=", 2) == 0) { op = OP_ADD; oplen = 2; }
        else if (strncmp(state->p, "-=", 2) == 0) { op = OP_SUB; oplen = 2; }
        else if (strncmp(state->p, "*=", 2) == 0) { op = OP_MUL; oplen = 2; }
        else if (strncmp(state->p, "/=", 2) == 0) { op = OP_DIV; oplen = 2; }
        else if (strncmp(state->p, "%=", 2) == 0) { op = OP_MOD; oplen = 2; }
        else if (strncmp(state->p, "<<=", 3) == 0) { op = OP_SHL; oplen = 3; }
        else if (strncmp(state->p, ">>=", 3) == 0) { op = OP_SHR; oplen = 3; }
        else if (strncmp(state->p, "&=", 2) == 0) { op = OP_AND; oplen = 2; }
        else if (strncmp(state->p, "^=", 2) == 0) { op = OP_XOR; oplen = 2; }
        else if (strncmp(state->p, "|=", 2) == 0) { op = OP_OR; oplen = 2; }

        if (op != OP_NONE) {
            state->p += oplen;
            long long rhs = parse_assignment(state);
            if (state->err) return 0;

            const char *val = get_shell_var(name);
            if (!val) val = getenv(name);
            long long cur = 0;
            if (val && parse_ll(val, &cur) < 0) {
                if (errno == ERANGE)
                    arith_set_error("overflow");
                else
                    arith_set_error("invalid number");
            }

            long long newv = 0;
            switch (op) {
                case OP_ADD:
                    if (add_overflow(cur, rhs, &newv)) {
                        arith_set_error("overflow");
                        return 0;
                    }
                    break;
                case OP_SUB:
                    if (sub_overflow(cur, rhs, &newv)) {
                        arith_set_error("overflow");
                        return 0;
                    }
                    break;
                case OP_MUL:
                    if (mul_overflow(cur, rhs, &newv)) {
                        arith_set_error("overflow");
                        return 0;
                    }
                    break;
                case OP_DIV:
                    if (rhs == 0) {
                        arith_set_error("divide by zero");
                        return 0;
                    }
                    if (cur == LLONG_MIN && rhs == -1) {
                        arith_set_error("overflow");
                        return 0;
                    }
                    newv = cur / rhs;
                    break;
                case OP_MOD:
                    if (rhs == 0) {
                        arith_set_error("divide by zero");
                        return 0;
                    }
                    if (cur == LLONG_MIN && rhs == -1) {
                        arith_set_error("overflow");
                        return 0;
                    }
                    newv = cur % rhs;
                    break;
                case OP_SHL:
                    if (lshift_overflow(cur, rhs, &newv)) {
                        arith_set_error("overflow");
                        return 0;
                    }
                    break;
                case OP_SHR:
                    if (rshift_overflow(cur, rhs, &newv)) {
                        arith_set_error("overflow");
                        return 0;
                    }
                    break;
                case OP_AND:
                    newv = cur & rhs;
                    break;
                case OP_XOR:
                    newv = cur ^ rhs;
                    break;
                case OP_OR:
                    newv = cur | rhs;
                    break;
                default:
                    break;
            }

            char buf[32];
            snprintf(buf, sizeof(buf), "%lld", newv);
            set_shell_var(name, buf);
            return newv;
        }

        /* Retrieve current variable value */
        const char *val = get_shell_var(name);
        if (!val) val = getenv(name);
        long long cur = 0;
        if (val && parse_ll(val, &cur) < 0) {
            if (errno == ERANGE)
                arith_set_error("overflow");
            else
                arith_set_error("invalid number");
        }

        if (prefix) {
            long long newv;
            if (add_overflow(cur, (incop == '+') ? 1 : -1, &newv)) {
                arith_set_error("overflow");
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
                arith_set_error("overflow");
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
long long eval_arith(const char *expr, int *err, char **errmsg) {
    ArithState st = { .p = expr, .err = 0, .err_msg = "" };
    current_state = &st;
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
        arith_set_error("syntax error");
    current_state = NULL;
    if (err)
        *err = st.err;
    if (errmsg)
        *errmsg = NULL;
    if (st.err) {
        if (errmsg)
            *errmsg = strdup(st.err_msg[0] ? st.err_msg : "error");
        else
            fprintf(stderr, "arith: %s\n", st.err_msg[0] ? st.err_msg : "error");
        return 0;
    }
    return result;
}
