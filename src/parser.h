#ifndef PARSER_H
#define PARSER_H

#include <stddef.h>

#define MAX_TOKENS 64
#define MAX_LINE 1024

char *expand_var(const char *token);
int parse_line(char *line, char **args, int *background);

#endif /* PARSER_H */
