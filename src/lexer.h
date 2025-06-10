#ifndef LEXER_H
#define LEXER_H

char *read_token(char **p, int *quoted);
char *expand_var(const char *token);
char *expand_history(const char *line);
char *expand_prompt(const char *prompt);
char **expand_braces(const char *word, int *count_out);

#endif /* LEXER_H */
