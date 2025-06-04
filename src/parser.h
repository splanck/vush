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
    char *in_file;
    char *out_file;
    int append;
    struct PipelineSegment *next;
} PipelineSegment;

typedef enum {
    OP_NONE,
    OP_SEMI,
    OP_AND,
    OP_OR
} CmdOp;

typedef struct Command {
    PipelineSegment *pipeline;
    int background;
    CmdOp op; /* operator connecting to next command */
    struct Command *next;
} Command;

char *expand_var(const char *token);
Command *parse_line(char *line);
void free_pipeline(PipelineSegment *p);
void free_commands(Command *c);

#endif /* PARSER_H */
