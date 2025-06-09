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
    char **assigns;   /* NAME=value pairs preceding the command */
    int assign_count;
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
    CMD_FUNCDEF,
    CMD_IF,
    CMD_FOR,
    CMD_WHILE,
    CMD_UNTIL,
    CMD_CASE,
    CMD_SUBSHELL,
    CMD_GROUP
} CmdType;

typedef struct CaseItem {
    char **patterns;
    int pattern_count;
    struct Command *body;
    int fall_through;
    struct CaseItem *next;
} CaseItem;

typedef struct Command {
    CmdType type;
    PipelineSegment *pipeline;
    struct Command *cond;     /* for if/while condition */
    struct Command *body;     /* then/do body */
    struct Command *else_part;/* else or elif chain */
    char *var;                /* for for loop variable */
    char **words;             /* for for loop word list */
    int word_count;
    char *text;               /* function body as text */
    CaseItem *cases;          /* for case clause items */
    struct Command *group;    /* commands for subshell or group */
    int background;
    CmdOp op; /* operator connecting to next command */
    struct Command *next;
} Command;

Command *parse_line(char *line);
char *read_continuation_lines(FILE *f, char *buf, size_t size);
void free_pipeline(PipelineSegment *p);
void free_commands(Command *c);
extern FILE *parse_input;

#endif /* PARSER_H */
