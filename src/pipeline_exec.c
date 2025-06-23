/*
 * High-level pipeline execution helpers.
 *
 * This module wraps the low level primitives from pipeline.c with
 * expansion, temporary assignment handling, background execution
 * and timing support.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <glob.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <stdbool.h>

#include "pipeline_exec.h"
#include "pipeline.h"
#include "builtins.h"
#include "vars.h"
#include "func_exec.h"
#include "options.h"
#include "util.h"
#include "hash.h"
#include "var_expand.h"
#include "redir.h"
#include "assignment_utils.h"
#include "parser.h"


static int spawn_pipeline_segments(PipelineSegment *pipeline, int background,
                                   const char *line);

/* Determine if a command name corresponds to a builtin. */
static int is_builtin_command(const char *name) {
    if (!name)
        return 0;
    for (int i = 0; i < BI_COUNT; i++) {
        if (strcmp(name, builtin_table[i].name) == 0)
            return 1;
    }
    return 0;
}

/* Execute a builtin or function with any redirections in the current shell. */
static int run_builtin_shell(PipelineSegment *seg) {
    struct redir_save sv; if (apply_redirs_shell(seg, &sv) < 0) { last_status = 1; return 1; }
    int handled = 0;
    if (is_builtin_command(seg->argv[0])) {
        run_builtin(seg->argv);
        handled = 1;
    } else {
        FuncEntry *fn = find_function(seg->argv[0]);
        if (fn) {
            run_function(fn, seg->argv);
            handled = 1;
        }
    }
    restore_redirs_shell(seg, &sv);
    return handled;
}

/* Expand only the temporary assignment words of SEG using the current environment. */
static void expand_temp_assignments(PipelineSegment *seg) {
    for (int i = 0; i < seg->assign_count; i++) {
        char *eq = strchr(seg->assigns[i], '=');
        if (eq) {
            char *name = strndup(seg->assigns[i], eq - seg->assigns[i]);
            char *val = expand_var(eq + 1);
            if (val) {
                size_t len = strlen(val);
                if (len >= 2 && ((val[0] == '\'' && val[len - 1] == '\'') ||
                                 (val[0] == '"' && val[len - 1] == '"'))) {
                    char *trim = strndup(val + 1, len - 2);
                    if (trim) { free(val); val = trim; }
                }
            }
            char *tmp = NULL;
            if (name && val)
                xasprintf(&tmp, "%s=%s", name, val);
            if (tmp) {
                free(seg->assigns[i]);
                seg->assigns[i] = tmp;
            }
            free(name);
            free(val);
        } else {
            char *new = expand_var(seg->assigns[i]);
            if (new) {
                free(seg->assigns[i]);
                seg->assigns[i] = new;
            }
        }
    }
}

static void expand_segment(PipelineSegment *seg) {
    char *newargv[MAX_TOKENS];
    int ai = 0;

    for (int i = 0; seg->argv[i] && ai < MAX_TOKENS - 1; i++) {
        char *word = seg->argv[i];
        if (seg->expand[i]) {
            char *exp = expand_var(word);
            if (!exp) exp = strdup("");

            size_t start = (size_t)ai;
            if (!seg->quoted[i]) {
                int count = 0;
                char **fields = split_fields(exp, &count);
                free(exp);
                if (fields) {
                    for (int f = 0; f < count && ai < MAX_TOKENS - 1; f++) {
                        char *fld = fields[f];
                        if (!opt_noglob &&
                            (strchr(fld, '*') || strchr(fld, '?'))) {
                            glob_t g;
                            int r = glob(fld, 0, NULL, &g);
                            if (r == 0 && g.gl_pathc > 0) {
                                size_t gstart = (size_t)ai;
                                for (size_t gi = 0; gi < g.gl_pathc &&
                                                 ai < MAX_TOKENS - 1; gi++) {
                                    char *dup = strdup(g.gl_pathv[gi]);
                                    if (!dup) {
                                        while ((size_t)ai > gstart) {
                                            free(newargv[--ai]);
                                            newargv[ai] = NULL;
                                        }
                                        free(fld);
                                        globfree(&g);
                                        goto skip_field;
                                    }
                                    newargv[ai] = dup;
                                    seg->expand[ai] = 0;
                                    seg->quoted[ai] = 0;
                                    ai++;
                                }
                                free(fld);
                                globfree(&g);
                                continue;
                            }
                            globfree(&g);
                        }
                        newargv[ai] = fld;
                        seg->expand[ai] = 0;
                        seg->quoted[ai] = 0;
                        ai++;
                    skip_field: ;
                    }
                    free(fields);
                } else {
                    exp = strdup("");
                    newargv[ai] = exp;
                    seg->expand[ai] = 0;
                    seg->quoted[ai] = 0;
                    ai++;
                }
            } else {
                newargv[ai] = exp;
                seg->expand[ai] = 0;
                seg->quoted[ai] = 0;
                ai++;
            }

            int changed = 1;
            if ((size_t)ai == start + 1 && strcmp(newargv[start], word) == 0)
                changed = 0;
            if (!changed) {
                free(newargv[start]);
                newargv[start] = word;
                seg->quoted[start] = seg->quoted[i];
            } else {
                free(word);
                seg->argv[i] = NULL;
            }
        } else {
            newargv[ai] = word;
            seg->expand[ai] = 0;
            seg->quoted[ai] = seg->quoted[i];
            ai++;
        }
    }
    newargv[ai] = NULL;
    seg->expand[ai] = 0;
    seg->quoted[ai] = 0;

    for (int j = ai; seg->argv[j]; j++)
        free(seg->argv[j]);

    for (int j = 0; j <= ai; j++) {
        seg->argv[j] = newargv[j];
        seg->expand[j] = 0;
        seg->quoted[j] = 0;
    }
    for (int j = ai + 1; j < MAX_TOKENS; j++) {
        seg->argv[j] = NULL;
        seg->expand[j] = 0;
        seg->quoted[j] = 0;
    }

    for (int i = 0; i < seg->assign_count; i++) {
        char *eq = strchr(seg->assigns[i], '=');
        if (eq) {
            char *name = strndup(seg->assigns[i], eq - seg->assigns[i]);
            char *val = expand_var(eq + 1);
            if (val) {
                size_t len = strlen(val);
                if (len >= 2 && ((val[0] == '\'' && val[len - 1] == '\'') ||
                                 (val[0] == '"' && val[len - 1] == '"'))) {
                    char *trim = strndup(val + 1, len - 2);
                    if (trim) { free(val); val = trim; }
                }
            }
            char *tmp = NULL;
            if (name && val)
                xasprintf(&tmp, "%s=%s", name, val);
            if (tmp) {
                free(seg->assigns[i]);
                seg->assigns[i] = tmp;
            }
            free(name);
            free(val);
        } else {
            char *new = expand_var(seg->assigns[i]);
            if (new) {
                free(seg->assigns[i]);
                seg->assigns[i] = new;
            }
        }
    }

    if (seg->in_file) {
        char *n = expand_var(seg->in_file);
        free(seg->in_file);
        seg->in_file = n;
    }

    if (seg->out_file && seg->err_file && seg->out_file == seg->err_file) {
        char *n = expand_var(seg->out_file);
        free(seg->out_file);
        seg->out_file = n;
        seg->err_file = seg->out_file;
    } else {
        if (seg->out_file) {
            char *n = expand_var(seg->out_file);
            free(seg->out_file);
            seg->out_file = n;
        }

        if (seg->err_file) {
            char *n = expand_var(seg->err_file);
            free(seg->err_file);
            seg->err_file = n;
        }
    }
}

static void expand_pipeline(PipelineSegment *pipeline) {
    for (PipelineSegment *seg = pipeline; seg; seg = seg->next)
        expand_segment(seg);
}

/* Expand all parts of SEG except for temporary assignments. */
static void expand_segment_no_assign(PipelineSegment *seg) {
    if (getenv("VUSH_DEBUG")) {
        fprintf(stderr, "expand_segment_no_assign before:");
        for (int i = 0; seg->argv[i]; i++)
            fprintf(stderr, " '%s'", seg->argv[i]);
        fprintf(stderr, "\n");
    }

    int save = seg->assign_count;
    seg->assign_count = 0;
    expand_segment(seg);
    seg->assign_count = save;

    if (getenv("VUSH_DEBUG")) {
        fprintf(stderr, "expand_segment_no_assign after:");
        for (int i = 0; seg->argv[i]; i++)
            fprintf(stderr, " '%s'", seg->argv[i]);
        fprintf(stderr, "\n");
    }
}

/* Create a deep copy of a pipeline so that expansions can be performed
 * without modifying the original command.  Returns NULL on allocation failure. */
static PipelineSegment *copy_pipeline(PipelineSegment *src) {
    PipelineSegment *head = NULL;
    PipelineSegment **tail = &head;
    while (src) {
        PipelineSegment *seg = xcalloc(1, sizeof(*seg));

        int i = 0;
        while (src->argv[i]) {
            seg->argv[i] = strdup(src->argv[i]);
            if (!seg->argv[i]) {
                free_pipeline(seg);
                free_pipeline(head);
                return NULL;
            }
            seg->expand[i] = src->expand[i];
            seg->quoted[i] = src->quoted[i];
            i++;
        }
        seg->argv[i] = NULL;
        seg->quoted[i] = 0;

        seg->in_file = src->in_file ? strdup(src->in_file) : NULL;
        seg->here_doc = src->here_doc;
        seg->here_doc_quoted = src->here_doc_quoted;
        seg->out_file = src->out_file ? strdup(src->out_file) : NULL;
        seg->append = src->append;
        seg->force = src->force;
        seg->dup_out = src->dup_out;
        seg->close_out = src->close_out;
        if (src->err_file == src->out_file)
            seg->err_file = seg->out_file;
        else
            seg->err_file = src->err_file ? strdup(src->err_file) : NULL;
        seg->err_append = src->err_append;
        seg->dup_err = src->dup_err;
        seg->close_err = src->close_err;
        seg->out_fd = src->out_fd;
        seg->in_fd = src->in_fd;
        seg->assign_count = src->assign_count;
        if (src->assign_count > 0) {
            seg->assigns = xcalloc(src->assign_count, sizeof(char *));
            for (int j = 0; j < src->assign_count; j++) {
                seg->assigns[j] = strdup(src->assigns[j]);
                if (!seg->assigns[j]) {
                    free_pipeline(seg);
                    free_pipeline(head);
                    return NULL;
                }
            }
        }

        seg->next = NULL;
        *tail = seg;
        tail = &seg->next;
        src = src->next;
    }
    return head;
}

/* Export temporary assignments and return the previous values to be restored
 * later.  When no command is present, assignments are applied permanently and
 * NULL is returned. */
static struct assign_backup *set_temp_environment(PipelineSegment *pipeline) {
    if (!pipeline->argv[0]) {
        for (int i = 0; i < pipeline->assign_count; i++) {
            char *eq = strchr(pipeline->assigns[i], '=');
            if (!eq)
                continue;
            char *name = strndup(pipeline->assigns[i], eq - pipeline->assigns[i]);
            if (!name)
                continue;
            char *val = eq + 1;
            size_t vlen = strlen(val);
            if (vlen > 1 && val[0] == '(' && val[vlen - 1] == ')') {
                apply_array_assignment(name, val, opt_allexport);
            } else {
                set_shell_var(name, val);
                if (opt_allexport)
                    setenv(name, val, 1);
            }
            free(name);
        }
        last_status = 0;
        return NULL;
    }

    struct assign_backup *backs = backup_assignments(pipeline);
    if (pipeline->assign_count > 0 && !backs)
        return NULL;

    for (int i = 0; i < pipeline->assign_count; i++) {
        char *eq = strchr(pipeline->assigns[i], '=');
        if (!eq || !backs[i].name)
            continue;
        char *val = eq + 1;
        size_t vlen = strlen(val);
        if (vlen > 1 && val[0] == '(' && val[vlen - 1] == ')') {
            apply_array_assignment(backs[i].name, val, 1);
        } else {
            setenv(backs[i].name, val, 1);
            set_shell_var(backs[i].name, val);
        }
    }

    return backs;
}

/* Execute the command with temporary assignments already exported. */
static int run_temp_command(PipelineSegment *pipeline, int background,
                            const char *line) {
    expand_segment_no_assign(pipeline);
    if (!pipeline->argv[0] || pipeline->argv[0][0] == '\0') {
        if (pipeline->argv[0]) {
            free(pipeline->argv[0]);
            pipeline->argv[0] = NULL;
        }
        return -1;        /* nothing to execute after expansion */
    }

    int has_redir =
        pipeline->in_file || pipeline->out_file || pipeline->err_file ||
        pipeline->dup_out != -1 || pipeline->dup_err != -1 ||
        pipeline->close_out || pipeline->close_err ||
        pipeline->out_fd != STDOUT_FILENO || pipeline->in_fd != STDIN_FILENO;

    int handled = 0;
    if (!pipeline->argv[0])
        return 0;
    int is_blt = is_builtin_command(pipeline->argv[0]);
    FuncEntry *fn = NULL;
    if (!is_blt)
        fn = find_function(pipeline->argv[0]);

    if (has_redir && (is_blt || fn) && !background) {
        handled = run_builtin_shell(pipeline);
    } else if (is_blt) {
        run_builtin(pipeline->argv);
        handled = 1;
    } else if (fn) {
        run_function(fn, pipeline->argv);
        handled = 1;
    } else if (has_redir) {
        spawn_pipeline_segments(pipeline, background, line);
        handled = 1;
    }

    return handled;
}

/* Restore the previous environment saved by set_temp_environment(). */
static void restore_temp_environment(PipelineSegment *pipeline,
                                     struct assign_backup *backs) {
    restore_assignments(pipeline, backs);
}

/* Apply temporary variable assignments before running a pipeline. */
static int apply_temp_assignments(PipelineSegment *pipeline, int background,
                                  const char *line) {
    if (pipeline->next)
        return 0;

    /* Expand assignment values using the current environment before setting
     * them so that subsequent word expansions see the temporary bindings. */
    expand_temp_assignments(pipeline);

    struct assign_backup *backs = set_temp_environment(pipeline);

    if (!pipeline->argv[0])
        return 0;

    if (pipeline->assign_count > 0 && !backs)
        return 1;

    int handled = run_temp_command(pipeline, background, line);
    if (handled == -1) {
        restore_temp_environment(pipeline, backs);
        set_temp_environment(pipeline);
        return 1;
    }

    restore_temp_environment(pipeline, backs);

    if (handled && opt_errexit && last_status != 0)
        exit(last_status);

    return handled;
}

/* Fork and execute each segment of a pipeline, wiring up pipes between
 * processes.  wait_for_pipeline() is used to collect child statuses when
 * running in the foreground.  Returns the value assigned to last_status. */
static int spawn_pipeline_segments(PipelineSegment *pipeline, int background,
                                   const char *line) {
    int seg_count = 0;
    for (PipelineSegment *tmp = pipeline; tmp; tmp = tmp->next)
        seg_count++;
    pid_t *pids = xcalloc(seg_count, sizeof(pid_t));

    int spawned = 0;
    int in_fd = -1;
    for (PipelineSegment *seg = pipeline; seg; seg = seg->next) {
        pid_t pid = fork_segment(seg, &in_fd);
        if (pid < 0) {
            if (in_fd != -1)
                close(in_fd);
            /* Wait for already spawned children */
            if (spawned > 0)
                wait_for_pipeline(pids, spawned, 0, line);
            free(pids);
            last_status = 1;
            return 1;
        }
        pids[spawned++] = pid;
    }

    if (in_fd != -1)
        close(in_fd);

    wait_for_pipeline(pids, spawned, background, line);
    free(pids);
    return last_status;
}

int run_pipeline_internal(PipelineSegment *pipeline, int background, const char *line) {
    if (!pipeline)
        return 0;

    PipelineSegment *copy = copy_pipeline(pipeline);
    if (!copy) {
        last_status = 1;
        return 1;
    }

    param_error = 0;
    if (opt_xtrace && line) {
        const char *ps4 = getenv("PS4");
        if (!ps4) ps4 = "+ ";
        fprintf(stderr, "%s%s\n", ps4, line);
    }

    int handled = apply_temp_assignments(copy, background, line);
    if (handled || (!copy->argv[0] && copy->assign_count > 0)) {
        if (param_error)
            last_status = 1;
        free_pipeline(copy);
        cleanup_proc_subs();
        if (opt_errexit && last_status != 0)
            exit(last_status);
        return last_status;
    }

    expand_pipeline(copy);

    if (!copy->argv[0] || copy->argv[0][0] == '\0') {
        fprintf(stderr, "syntax error: missing command\n");
        last_status = 1;
        free_pipeline(copy);
        cleanup_proc_subs();
        return last_status;
    }
    int r = spawn_pipeline_segments(copy, background, line);
    if (param_error)
        last_status = 1;
    free_pipeline(copy);
    cleanup_proc_subs();
    if (opt_errexit && !background && last_status != 0)
        exit(last_status);
    return r;
}

struct time_data { PipelineSegment *pipeline; int background; const char *line; };

static int pipeline_cb(void *arg)
{
    struct time_data *td = arg;
    return run_pipeline_internal(td->pipeline, td->background, td->line);
}

int run_pipeline_timed(PipelineSegment *pipeline, int background, const char *line)
{
    struct time_data td = { pipeline, background, line };
    int status = builtin_time_callback(pipeline_cb, &td, 0);
    last_status = status;
    if (opt_errexit && !background && last_status != 0)
        exit(last_status);
    return status;
}

