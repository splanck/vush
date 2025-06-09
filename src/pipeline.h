#ifndef PIPELINE_H
#define PIPELINE_H
#include "parser.h"
#include <sys/types.h>

void setup_child_pipes(PipelineSegment *seg, int in_fd, int pipefd[2]);
pid_t fork_segment(PipelineSegment *seg, int *in_fd);
void wait_for_pipeline(pid_t *pids, int count, int background, const char *line);

#endif /* PIPELINE_H */
