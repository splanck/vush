/*
 * vush - a simple UNIX shell
 * Licensed under the BSD 2-Clause Simplified License.
 * Tokenization and expansion helpers.
 */

/*
 * Lexing helpers used by the parser.  These functions read tokens from an
 * input string and perform shell expansions.  Expansion covers variable,
 * history and prompt substitution as well as simple brace patterns.
 */
#ifndef LEXER_H
#define LEXER_H

/* Read the next token from *p applying quoting and command substitution.
 * The returned string is newly allocated and must be freed by the caller.
 * QUOTED is set non-zero when the token originated from quoted text. */
char *read_token(char **p, int *quoted, int *do_expand);


/* Parse a command substitution expression starting at *p. Used internally by
 * the token reader but exposed for cross-module use. */
char *parse_substitution(char **p);

#endif /* LEXER_H */
