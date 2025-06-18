#ifndef PIPELINE_EXEC_H
#define PIPELINE_EXEC_H

#include "parser.h"

int run_pipeline_internal(PipelineSegment *pipeline, int background, const char *line);
int run_pipeline_timed(PipelineSegment *pipeline, int background, const char *line);

#endif /* PIPELINE_EXEC_H */
