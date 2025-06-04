#ifndef EXECUTE_H
#define EXECUTE_H

#include "parser.h"

int run_pipeline(PipelineSegment *pipeline, int background, const char *line);

#endif /* EXECUTE_H */
