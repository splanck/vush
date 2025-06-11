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
    int force;       /* >| force overwrite */
    int dup_out;      /* > &N duplication */
    int close_out;    /* >&- close descriptor */
    char *err_file;
    int err_append;
    int dup_err;      /* 2>&N duplication */
    int close_err;    /* 2>&- close descriptor */
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
    CMD_SELECT,
    CMD_FOR_ARITH,
    CMD_WHILE,
    CMD_UNTIL,
    CMD_CASE,
    CMD_SUBSHELL,
    CMD_GROUP,
    CMD_COND
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
    char **words;             /* for loop word list or [[ expression ]] */
    int word_count;
    char *arith_init;         /* for arithmetic for loop */
    char *arith_cond;
    char *arith_update;
    char *text;               /* function body as text */
    CaseItem *cases;          /* for case clause items */
    struct Command *group;    /* commands for subshell or group */
    int negate;               /* invert status with leading ! */
    int background;
    CmdOp op; /* operator connecting to next command */
    struct Command *next;
} Command;

Command *parse_line(char *line);
char *read_continuation_lines(FILE *f, char *buf, size_t size);
char *gather_until(char **p, const char **stops, int nstops, int *idx);
char *gather_braced(char **p);
char *gather_parens(char **p);
char *gather_dbl_parens(char **p);
char *trim_ws(const char *s);
char *process_substitution(char **p, int read_from);
Command *parse_function_def(char **p, CmdOp *op_out);
Command *parse_subshell(char **p, CmdOp *op_out);
Command *parse_brace_group(char **p, CmdOp *op_out);
Command *parse_conditional(char **p, CmdOp *op_out);
Command *parse_control_clause(char **p, CmdOp *op_out);
void free_case_items(CaseItem *ci);
void free_pipeline(PipelineSegment *p);
void free_commands(Command *c);
void cleanup_proc_subs(void);
extern FILE *parse_input;
extern int parse_need_more;

#endif /* PARSER_H */
