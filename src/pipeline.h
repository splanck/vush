/*
 * vush - a simple UNIX shell
 * Licensed under the BSD 2-Clause Simplified License.
 * Pipeline process utilities.
 */

#ifndef PIPELINE_H
#define PIPELINE_H
/*
 * Low-level helpers for spawning and waiting on command pipelines.
 * These declarations are used by the executor to set up pipes,
 * fork each pipeline segment and collect their statuses.
 */
#include "parser.h"
#include <sys/types.h>

/* Prepare SEG's child to read from IN_FD and optionally write to PIPEFD. */
void setup_child_pipes(PipelineSegment *seg, int in_fd, int pipefd[2]);

/* Spawn SEG's command and update IN_FD with the read end for the next stage. */
pid_t fork_segment(PipelineSegment *seg, int *in_fd);

/* Wait for all COUNT processes in PIDS.  If BACKGROUND is non-zero the job
 * is recorded using LINE instead of blocking. */
void wait_for_pipeline(pid_t *pids, int count, int background, const char *line);

#endif /* PIPELINE_H */
