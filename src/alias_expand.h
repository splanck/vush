/*
 * vush - a simple UNIX shell
 * Licensed under the BSD 2-Clause Simplified License.
 * Alias expansion helpers.
 *
 * Declarations for alias substitution routines.
 */

#ifndef ALIAS_EXPAND_H
#define ALIAS_EXPAND_H
#include "parser.h"

int expand_aliases_in_segment(PipelineSegment *seg, int *argc, char *tok);

#endif /* ALIAS_EXPAND_H */
