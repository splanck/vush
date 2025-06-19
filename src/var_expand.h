#ifndef VAR_EXPAND_H
#define VAR_EXPAND_H

char *expand_var(const char *token);
char *ansi_unescape(const char *src);
char *expand_simple(const char *token);
char **split_fields(const char *text, int *count);
char *expand_prompt(const char *prompt);

#endif /* VAR_EXPAND_H */
