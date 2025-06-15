/*
 * Main parser entry points linking the helper modules.
 */
#include "parser.h"
#include <stdlib.h>
#include <unistd.h>

FILE *parse_input = NULL;
int parse_need_more = 0;
int parse_noexpand = 0;

/* Free a linked list of PipelineSegment structures */
void free_pipeline(PipelineSegment *p) {
    while (p) {
        PipelineSegment *next = p->next;
        for (int i = 0; p->argv[i]; i++)
            free(p->argv[i]);
        for (int i = 0; i < p->assign_count; i++)
            free(p->assigns[i]);
        free(p->assigns);
        if (p->here_doc && p->in_file)
            unlink(p->in_file);
        free(p->in_file);
        free(p->out_file);
        /* err_file may share the same allocation as out_file */
        if (p->err_file && p->err_file != p->out_file)
            free(p->err_file);
        free(p);
        p = next;
    }
}

void free_case_items(CaseItem *ci);

/* Recursively free a chain of Command structures */
void free_commands(Command *c) {
    while (c) {
        Command *next = c->next;
        if (c->type == CMD_PIPELINE) {
            free_pipeline(c->pipeline);
        } else if (c->type == CMD_IF) {
            free_commands(c->cond);
            free_commands(c->body);
            free_commands(c->else_part);
        } else if (c->type == CMD_WHILE || c->type == CMD_UNTIL) {
            free_commands(c->cond);
            free_commands(c->body);
        } else if (c->type == CMD_FOR) {
            free(c->var);
            for (int i = 0; i < c->word_count; i++)
                free(c->words[i]);
            free(c->words);
            free_commands(c->body);
        } else if (c->type == CMD_SELECT) {
            free(c->var);
            for (int i = 0; i < c->word_count; i++)
                free(c->words[i]);
            free(c->words);
            free_commands(c->body);
        } else if (c->type == CMD_FOR_ARITH) {
            free(c->arith_init);
            free(c->arith_cond);
            free(c->arith_update);
            free_commands(c->body);
        } else if (c->type == CMD_CASE) {
            free(c->var);
            free_case_items(c->cases);
        } else if (c->type == CMD_FUNCDEF) {
            free(c->var);
            free(c->text);
            free_commands(c->body);
        } else if (c->type == CMD_SUBSHELL || c->type == CMD_GROUP) {
            free_commands(c->group);
        } else if (c->type == CMD_COND) {
            for (int i = 0; i < c->word_count; i++)
                free(c->words[i]);
            free(c->words);
        }
        free(c);
        c = next;
    }
}

