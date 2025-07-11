/*
 * vush - a simple UNIX shell
 * Licensed under the BSD 2-Clause Simplified License.
 * Here-document helpers.
 */

#ifndef PARSER_HERE_DOC_H
#define PARSER_HERE_DOC_H
#include "parser.h"

int process_here_doc(PipelineSegment *seg, char **p, char *tok, int quoted);
int parse_here_string(PipelineSegment *seg, char **p, char *tok);

#endif /* PARSER_HERE_DOC_H */
