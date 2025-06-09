/*
 * vush - a simple UNIX shell
 * Licensed under the GNU GPLv3.
 */

#ifndef PARSER_H
#define PARSER_H

#include <stddef.h>
#include <stdio.h>

#define MAX_TOKENS 64
#define MAX_LINE 1024

typedef struct PipelineSegment {
    char *argv[MAX_TOKENS];
    char *in_file;
    int here_doc;     /* input file is temporary here-doc */
    char *out_file;
    int append;
    int dup_out;      /* > &N duplication */
    char *err_file;
    int err_append;
    int dup_err;      /* 2>&N duplication */
    struct PipelineSegment *next;
} PipelineSegment;

typedef enum {
    OP_NONE,
    OP_SEMI,
    OP_AND,
    OP_OR
} CmdOp;

typedef enum {
    CMD_PIPELINE,
    CMD_IF,
    CMD_FOR,
    CMD_WHILE,
    CMD_CASE
} CmdType;

typedef struct Command {
    CmdType type;
    PipelineSegment *pipeline;
    struct Command *cond;     /* for if/while condition */
    struct Command *body;     /* then/do body */
    struct Command *else_part;/* else or elif chain */
    char *var;                /* for for loop variable */
    char **words;             /* for for loop word list */
    int word_count;
    int background;
    CmdOp op; /* operator connecting to next command */
    struct Command *next;
} Command;

char *expand_var(const char *token);
char *expand_prompt(const char *prompt);
char *expand_history(const char *line);
Command *parse_line(char *line);
void free_pipeline(PipelineSegment *p);
void free_commands(Command *c);
extern FILE *parse_input;

#endif /* PARSER_H */
