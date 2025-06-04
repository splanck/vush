/*
 * vush - a simple UNIX shell
 * Licensed under the GNU GPLv3.
 */

#ifndef PARSER_H
#define PARSER_H

#include <stddef.h>

#define MAX_TOKENS 64
#define MAX_LINE 1024

typedef struct PipelineSegment {
    char *argv[MAX_TOKENS];
    struct PipelineSegment *next;
} PipelineSegment;

char *expand_var(const char *token);
PipelineSegment *parse_line(char *line, int *background);
void free_pipeline(PipelineSegment *p);

#endif /* PARSER_H */
