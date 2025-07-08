/*
 * vush - a simple UNIX shell
 * Licensed under the BSD 2-Clause Simplified License.
 * Variable assignment helpers.
 */

#ifndef ASSIGNMENT_UTILS_H
#define ASSIGNMENT_UTILS_H

#include "parser.h"

struct assign_backup {
    char *name;
    char *env;
    char *var;
    int had_env;
    int had_var;
};

char **parse_array_values(const char *val, int *count);
void apply_array_assignment(const char *name, const char *val, int export_env);
void expand_assignment(char **assign);
struct assign_backup *backup_assignments(PipelineSegment *pipeline);
void restore_assignments(PipelineSegment *pipeline, struct assign_backup *backs);

#endif /* ASSIGNMENT_UTILS_H */
