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

/* Expand variable, arithmetic and tilde expressions found in TOKEN.
 * Returns a newly allocated string that the caller must free. */
char *expand_var(const char *token);

/* Split TEXT into fields using characters from $IFS.  Returns a malloc'd
 * array of strings terminated by NULL. COUNT is set to the number of fields. */
char **split_fields(const char *text, int *count);

/* Apply history expansion to LINE when it begins with '!'.  The expanded
 * line is returned as a new string or NULL on error and must be freed. */
char *expand_history(const char *line);

/* Expand escape sequences and variables that appear in PROMPT using the
 * normal token rules.  The caller must free the returned string. */
char *expand_prompt(const char *prompt);

/* Expand simple brace expressions in WORD.  COUNT_OUT is set to the number of
 * resulting words.  Returns a malloc'd NULL-terminated array of strings which
 * the caller must free. */
char **expand_braces(const char *word, int *count_out);

/* Parse a command substitution expression starting at *p. Used internally by
 * the token reader but exposed for cross-module use. */
char *parse_substitution(char **p);

#endif /* LEXER_H */
