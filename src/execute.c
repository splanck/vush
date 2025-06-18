/*
 * Execution engine for running parsed command lists.
 *
 * This module drives pipelines, builtins, functions and handles all
 * shell control flow constructs such as loops and conditionals.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <fnmatch.h>
#include <glob.h>

#include "execute.h"
#include "jobs.h"
#include "builtins.h"
#include "vars.h"
#include "scriptargs.h"
#include "options.h"
#include "pipeline.h"
#include "func_exec.h"
#include "arith.h"
#include "util.h"
#include "hash.h"
#include "lexer.h"

extern int last_status;
extern int param_error;

int loop_break = 0;
int loop_continue = 0;
int loop_depth = 0;

int run_command_list(Command *cmds, const char *line);
static int apply_temp_assignments(PipelineSegment *pipeline, int background,
                                  const char *line);
void setup_redirections(PipelineSegment *seg);
static int spawn_pipeline_segments(PipelineSegment *pipeline, int background,
                                   const char *line);
static int exec_pipeline(Command *cmd, const char *line);
static int exec_funcdef(Command *cmd, const char *line);
static int exec_if(Command *cmd, const char *line);
static int exec_while(Command *cmd, const char *line);
static int exec_until(Command *cmd, const char *line);
static int exec_for(Command *cmd, const char *line);
static int exec_select(Command *cmd, const char *line);
static int exec_for_arith(Command *cmd, const char *line);
static int exec_case(Command *cmd, const char *line);
static int exec_subshell(Command *cmd, const char *line);
static int exec_cond(Command *cmd, const char *line);
static int exec_group(Command *cmd, const char *line);
static int exec_arith(Command *cmd, const char *line);

struct assign_backup {
    char *name;
    char *env;
    char *var;
    int had_env;
    int had_var;
};

static struct assign_backup *backup_assignments(PipelineSegment *pipeline);
static void restore_assignments(PipelineSegment *pipeline, struct assign_backup *backs);
static void apply_array_assignment(const char *name, const char *val, int export_env);
static char **parse_array_values(const char *val, int *count);

/* Determine if a command name corresponds to a builtin. */
static int is_builtin_command(const char *name) {
    for (int i = 0; builtin_table[i].name; i++) {
        if (strcmp(name, builtin_table[i].name) == 0)
            return 1;
    }
    return 0;
}

/* Apply redirections in the current process for builtin execution. */
struct redir_save { int in, out, err; };
static int apply_redirs_shell(PipelineSegment *seg, struct redir_save *sv) {
    sv->in = sv->out = sv->err = -1;
    if (seg->in_file) {
        sv->in = dup(seg->in_fd);
        int fd = open(seg->in_file, O_RDONLY);
        if (fd < 0) {
            perror(seg->in_file);
            return -1;
        }
        if (seg->here_doc)
            unlink(seg->in_file);
        dup2(fd, seg->in_fd);
        close(fd);
    }

    if (seg->out_file && seg->err_file && strcmp(seg->out_file, seg->err_file) == 0 &&
        seg->append == seg->err_append) {
        sv->out = dup(seg->out_fd);
        sv->err = dup(STDERR_FILENO);
        int fd = open_redirect(seg->out_file, seg->append, seg->force);
        if (fd < 0) {
            perror(seg->out_file);
            return -1;
        }
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        close(fd);
    } else {
        if (seg->out_file) {
            sv->out = dup(seg->out_fd);
            int fd = open_redirect(seg->out_file, seg->append, seg->force);
            if (fd < 0) {
                perror(seg->out_file);
                return -1;
            }
            dup2(fd, seg->out_fd);
            close(fd);
        }
        if (seg->err_file) {
            sv->err = dup(STDERR_FILENO);
            int fd = open_redirect(seg->err_file, seg->err_append, 0);
            if (fd < 0) {
                perror(seg->err_file);
                return -1;
            }
            dup2(fd, STDERR_FILENO);
            close(fd);
        }
    }

    if (seg->close_out) {
        if (sv->out == -1)
            sv->out = dup(seg->out_fd);
        close(seg->out_fd);
    } else if (seg->dup_out != -1) {
        if (sv->out == -1)
            sv->out = dup(seg->out_fd);
        dup2(seg->dup_out, seg->out_fd);
    }

    if (seg->close_err) {
        if (sv->err == -1)
            sv->err = dup(STDERR_FILENO);
        close(STDERR_FILENO);
    } else if (seg->dup_err != -1) {
        if (sv->err == -1)
            sv->err = dup(STDERR_FILENO);
        dup2(seg->dup_err, STDERR_FILENO);
    }

    return 0;
}

static void restore_redirs_shell(PipelineSegment *seg, struct redir_save *sv) {
    if (sv->in != -1) { dup2(sv->in, seg->in_fd); close(sv->in); }
    if (sv->out != -1) { dup2(sv->out, seg->out_fd); close(sv->out); }
    if (sv->err != -1) { dup2(sv->err, STDERR_FILENO); close(sv->err); }
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

/* Expand only the temporary assignment words of SEG using the current
 * environment. */
static void expand_assignments(PipelineSegment *seg) {
    for (int i = 0; i < seg->assign_count; i++) {
        char *eq = strchr(seg->assigns[i], '=');
        if (eq) {
            char *name = strndup(seg->assigns[i], eq - seg->assigns[i]);
            char *val = expand_var(eq + 1);
            char *tmp = NULL;
            if (name && val)
                asprintf(&tmp, "%s=%s", name, val);
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
            free(word);
            seg->argv[i] = NULL;
            if (!exp) exp = strdup("");
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
                                size_t start = (size_t)ai;
                                for (size_t gi = 0; gi < g.gl_pathc &&
                                                 ai < MAX_TOKENS - 1; gi++) {
                                    char *dup = strdup(g.gl_pathv[gi]);
                                    if (!dup) {
                                        while ((size_t)ai > start) {
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
                    continue;
                } else {
                    exp = strdup("");
                }
            }
            newargv[ai] = exp;
            seg->expand[ai] = 0;
            seg->quoted[ai] = 0;
            ai++;
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
            char *tmp = NULL;
            if (name && val)
                asprintf(&tmp, "%s=%s", name, val);
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
    int save = seg->assign_count;
    seg->assign_count = 0;
    expand_segment(seg);
    seg->assign_count = save;
}

/*
 * Create a deep copy of a pipeline so that expansions can be performed
 * without modifying the original command.  Returns NULL on allocation
 * failure.
 */
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
/*
 * Duplicate the given descriptor onto another descriptor and close the
 * original.  Used internally when applying redirections.  Does not
 * modify last_status.
 */
static void redirect_fd(int fd, int dest) {
    dup2(fd, dest);
    close(fd);
}

static char **parse_array_values(const char *val, int *count) {
    *count = 0;
    char *body = strndup(val + 1, strlen(val) - 2);
    if (!body)
        return NULL;

    char **vals = NULL;
    char *p = body;
    while (*p) {
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '\0')
            break;
        char *start = p;
        while (*p && *p != ' ' && *p != '\t')
            p++;
        if (*p)
            *p++ = '\0';

        char **tmp = realloc(vals, sizeof(char *) * (*count + 1));
        if (!tmp) {
            free(body);
            for (int i = 0; i < *count; i++)
                free(vals[i]);
            free(vals);
            *count = 0;
            return NULL;
        }
        vals = tmp;
        vals[*count] = strdup(start);
        if (!vals[*count]) {
            for (int i = 0; i < *count; i++)
                free(vals[i]);
            free(vals);
            free(body);
            *count = 0;
            return NULL;
        }
        (*count)++;
    }
    free(body);

    if (*count == 0) {
        vals = xcalloc(1, sizeof(char *));
    }

    return vals;
}

static void apply_array_assignment(const char *name, const char *val, int export_env) {
    int count = 0;
    char **vals = parse_array_values(val, &count);
    if (!vals && count > 0)
        return;

    set_shell_array(name, vals, count);

    if (export_env) {
        size_t joinlen = 0;
        for (int j = 0; j < count; j++)
            joinlen += strlen(vals[j]) + 1;
        char *joined = malloc(joinlen + 1);
        if (joined) {
            joined[0] = '\0';
            for (int j = 0; j < count; j++) {
                strcat(joined, vals[j]);
                if (j < count - 1)
                    strcat(joined, " ");
            }
            setenv(name, joined, 1);
            free(joined);
        }
    }

    for (int j = 0; j < count; j++)
        free(vals[j]);
    free(vals);
}

static struct assign_backup *backup_assignments(PipelineSegment *pipeline) {
    if (pipeline->assign_count == 0)
        return NULL;

    struct assign_backup *backs = xcalloc(pipeline->assign_count, sizeof(*backs));

    for (int i = 0; i < pipeline->assign_count; i++) {
        char *eq = strchr(pipeline->assigns[i], '=');
        if (!eq) {
            backs[i].name = NULL;
            continue;
        }
        backs[i].name = strndup(pipeline->assigns[i], eq - pipeline->assigns[i]);
        if (!backs[i].name) {
            backs[i].env = backs[i].var = NULL;
            backs[i].had_env = backs[i].had_var = 0;
            continue;
        }
        const char *oe = getenv(backs[i].name);
        backs[i].had_env = oe != NULL;
        backs[i].env = oe ? strdup(oe) : NULL;
        if (oe && !backs[i].env) {
            free(backs[i].name);
            backs[i].name = NULL;
            continue;
        }
        const char *ov = get_shell_var(backs[i].name);
        backs[i].had_var = ov != NULL;
        backs[i].var = ov ? strdup(ov) : NULL;
        if (ov && !backs[i].var) {
            free(backs[i].env);
            free(backs[i].name);
            backs[i].name = NULL;
            continue;
        }
    }

    return backs;
}

static void restore_assignments(PipelineSegment *pipeline, struct assign_backup *backs) {
    if (!backs)
        return;
    for (int i = 0; i < pipeline->assign_count; i++) {
        if (!backs[i].name)
            continue;
        if (backs[i].had_env)
            setenv(backs[i].name, backs[i].env, 1);
        else
            unsetenv(backs[i].name);
        if (backs[i].had_var)
            set_shell_var(backs[i].name, backs[i].var);
        else
            unset_shell_var(backs[i].name);
        free(backs[i].name);
        free(backs[i].env);
        free(backs[i].var);
    }
    free(backs);
}
/*
 * Apply temporary variable assignments before running a pipeline.
 * Builtins and functions are executed directly while environment
 * variables are preserved and restored around the call.
 */

static int apply_temp_assignments(PipelineSegment *pipeline, int background,
                                  const char *line) {
    if (pipeline->next)
        return 0;

    /* Expand assignment values using the current environment before setting
     * them so that subsequent word expansions see the temporary bindings. */
    expand_assignments(pipeline);

    if (!pipeline->argv[0] && pipeline->assign_count > 0) {
        for (int i = 0; i < pipeline->assign_count; i++) {
            char *eq = strchr(pipeline->assigns[i], '=');
            if (!eq)
                continue;
            char *name = strndup(pipeline->assigns[i], eq - pipeline->assigns[i]);
            if (!name)
                continue;
            char *val = eq + 1;
            size_t vlen = strlen(val);
            if (vlen > 1 && val[0] == '(' && val[vlen-1] == ')') {
                apply_array_assignment(name, val, opt_allexport);
            } else {
                set_shell_var(name, val);
                if (opt_allexport)
                    setenv(name, val, 1);
            }
            free(name);
        }
        last_status = 0;
        return 0;
    }

    if (!pipeline->argv[0])
        return 0;

    struct assign_backup *backs = backup_assignments(pipeline);
    if (pipeline->assign_count > 0 && !backs)
        return 1;

    for (int i = 0; i < pipeline->assign_count; i++) {
        char *eq = strchr(pipeline->assigns[i], '=');
        if (!eq || !backs[i].name)
            continue;
        char *val = eq + 1;
        size_t vlen = strlen(val);
        if (vlen > 1 && val[0] == '(' && val[vlen-1] == ')') {
            apply_array_assignment(backs[i].name, val, 1);
        } else {
            setenv(backs[i].name, val, 1);
            set_shell_var(backs[i].name, val);
        }
    }

    expand_segment_no_assign(pipeline);

    int has_redir =
        pipeline->in_file || pipeline->out_file || pipeline->err_file ||
        pipeline->dup_out != -1 || pipeline->dup_err != -1 ||
        pipeline->close_out || pipeline->close_err ||
        pipeline->out_fd != STDOUT_FILENO || pipeline->in_fd != STDIN_FILENO;

    int handled = 0;
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

    restore_assignments(pipeline, backs);

    if (handled && opt_errexit && last_status != 0)
        exit(last_status);

    return handled;
}

void setup_redirections(PipelineSegment *seg) {
    if (seg->in_file) {
        int fd = open(seg->in_file, O_RDONLY);
        if (fd < 0) {
            perror(seg->in_file);
            exit(1);
        }
        if (seg->here_doc)
            unlink(seg->in_file);
        redirect_fd(fd, seg->in_fd);
    }

    if (seg->out_file && seg->err_file && strcmp(seg->out_file, seg->err_file) == 0 &&
        seg->append == seg->err_append) {
        int fd = open_redirect(seg->out_file, seg->append, seg->force);
        if (fd < 0) {
            perror(seg->out_file);
            exit(1);
        }
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        close(fd);
    } else {
        if (seg->out_file) {
            int fd = open_redirect(seg->out_file, seg->append, seg->force);
            if (fd < 0) {
                perror(seg->out_file);
                exit(1);
            }
            redirect_fd(fd, seg->out_fd);
        }
        if (seg->err_file) {
            int fd = open_redirect(seg->err_file, seg->err_append, 0);
            if (fd < 0) {
                perror(seg->err_file);
                exit(1);
            }
            redirect_fd(fd, STDERR_FILENO);
        }
    }

    if (seg->close_out) {
        close(seg->out_fd);
        if (seg->out_fd == STDERR_FILENO)
            seg->close_err = 0;
        if (seg->dup_err == seg->out_fd)
            seg->dup_err = -1;
    } else if (seg->dup_out != -1) {
        dup2(seg->dup_out, seg->out_fd);
    }

    if (seg->close_err)
        close(STDERR_FILENO);
    else if (seg->dup_err != -1)
        dup2(seg->dup_err, STDERR_FILENO);
}


/*
 * Fork and execute each segment of a pipeline, wiring up pipes between
 * processes.  wait_for_pipeline() is used to collect child statuses when
 * running in the foreground.  Returns the value assigned to last_status.
 */
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

static int run_pipeline_internal(PipelineSegment *pipeline, int background, const char *line) {
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

int run_command_list(Command *cmds, const char *line) {
    CmdOp prev = OP_SEMI;
    for (Command *c = cmds; c; c = c->next) {
        int run = 1;
        if (c != cmds) {
            if (prev == OP_AND)
                run = (last_status == 0);
            else if (prev == OP_OR)
                run = (last_status != 0);
        }
        if (run) {
            if (opt_noexec) {
                if (c->type == CMD_PIPELINE && c->pipeline &&
                    c->pipeline->argv[0] &&
                    strcmp(c->pipeline->argv[0], "set") == 0)
                    run_pipeline(c, line);
            } else {
                run_pipeline(c, line);
            }
        }
        prev = c->op;
        if (func_return || loop_break || loop_continue)
            break;
    }
    if (loop_depth == 0) {
        loop_break = 0;
        loop_continue = 0;
    }
    return last_status;
}

/*
 * Execute a simple or composed pipeline.  This wraps
 * run_pipeline_internal() and returns its resulting status.
 */
static int exec_pipeline(Command *cmd, const char *line) {
    return run_pipeline_internal(cmd->pipeline, cmd->background, line);
}

/*
 * Define a shell function.  The body of the command becomes the new
 * function definition and last_status is preserved.
 */
static int exec_funcdef(Command *cmd, const char *line) {
    (void)line;
    define_function(cmd->var, NULL, cmd->text);
    free_commands(cmd->body);
    cmd->body = NULL;
    return last_status;
}

/*
 * Execute an if/else construct.  The condition list is run first and
 * the body or else part is selected based on its exit status.  Returns
 * the status of the last command executed.
 */
static int exec_if(Command *cmd, const char *line) {
    run_command_list(cmd->cond, line);
    if (last_status == 0)
        run_command_list(cmd->body, line);
    else if (cmd->else_part)
        run_command_list(cmd->else_part, line);
    return last_status;
}

/*
 * Execute a while loop.  The condition list is repeatedly evaluated
 * and the body executed while it returns success.  Loop control flags
 * loop_break and loop_continue are honored.  Returns last_status.
 */
static int exec_while(Command *cmd, const char *line) {
    loop_depth++;
    while (1) {
        run_command_list(cmd->cond, line);
        if (loop_break) { loop_break--; break; }
        if (loop_continue) {
            if (--loop_continue) {
                loop_depth--;
                return last_status;
            }
            continue;
        }
        if (last_status != 0)
            break;
        run_command_list(cmd->body, line);
        if (loop_break) { loop_break--; break; }
        if (loop_continue) {
            if (--loop_continue) {
                loop_depth--;
                return last_status;
            }
            continue;
        }
    }
    loop_depth--;
    return last_status;
}

/*
 * Execute an until loop.  This is the inverse of a while loop: the
 * body runs repeatedly until the condition list succeeds.  Loop
 * control flags are processed and the final last_status is returned.
 */
static int exec_until(Command *cmd, const char *line) {
    loop_depth++;
    while (1) {
        run_command_list(cmd->cond, line);
        if (loop_break) { loop_break--; break; }
        if (loop_continue) {
            if (--loop_continue) {
                loop_depth--;
                return last_status;
            }
            continue;
        }
        if (last_status == 0)
            break;
        run_command_list(cmd->body, line);
        if (loop_break) { loop_break--; break; }
        if (loop_continue) {
            if (--loop_continue) {
                loop_depth--;
                return last_status;
            }
            continue;
        }
    }
    loop_depth--;
    return last_status;
}

/*
 * Execute a for loop over a list of words.  The variable specified by
 * cmd->var is set to each word in turn before the body is executed.
 * Returns last_status from the final iteration.
 */
static int exec_for(Command *cmd, const char *line) {
    loop_depth++;
    for (int i = 0; i < cmd->word_count; i++) {
        char *exp = expand_var(cmd->words[i]);
        if (!exp) { loop_depth--; return last_status; }
        int count = 0;
        char **fields = split_fields(exp, &count);
        free(exp);
        for (int fi = 0; fi < count; fi++) {
            char *w = fields[fi];
            if (cmd->var) {
                set_shell_var(cmd->var, w);
                setenv(cmd->var, w, 1);
            }
            run_command_list(cmd->body, line);
            if (loop_break) break;
            if (loop_continue) {
                if (--loop_continue) {
                    for (int fj = fi; fj < count; fj++)
                        free(fields[fj]);
                    free(fields);
                    loop_depth--;
                    return last_status;
                }
                continue;
            }
        }
        for (int fj = 0; fj < count; fj++)
            free(fields[fj]);
        free(fields);
        if (loop_break) { loop_break--; break; }
    }
    loop_depth--;
    return last_status;
}

/*
 * Implement the POSIX select loop.  A numbered menu is printed for the
 * supplied words and user input chooses which value is assigned to the
 * iteration variable.  The function returns the status of the body.
 */
static int exec_select(Command *cmd, const char *line) {
    loop_depth++;
    char input[MAX_LINE];
    while (1) {
        for (int i = 0; i < cmd->word_count; i++)
            printf("%d) %s\n", i + 1, cmd->words[i]);
        const char *ps3 = getenv("PS3");
        fputs(ps3 ? ps3 : "? ", stdout);
        fflush(stdout);
        if (!fgets(input, sizeof(input), stdin))
            break;
        int choice = atoi(input);
        if (choice < 1 || choice > cmd->word_count) {
            continue;
        }
        if (cmd->var)
            setenv(cmd->var, cmd->words[choice - 1], 1);
        run_command_list(cmd->body, line);
        if (loop_break) { loop_break--; break; }
        if (loop_continue) {
            if (--loop_continue) {
                loop_depth--;
                return last_status;
            }
            continue;
        }
    }
    loop_depth--;
    return last_status;
}

/*
 * Execute a C-style for loop using arithmetic expressions.  The init,
 * condition and update parts are evaluated with eval_arith().  The loop
 * terminates when the condition evaluates to zero.  Returns last_status.
 */
static int exec_for_arith(Command *cmd, const char *line) {
    (void)line;
    int err = 0;
    char *msg = NULL;
    loop_depth++;

    eval_arith(cmd->arith_init ? cmd->arith_init : "0", &err, &msg);
    if (err) {
        if (msg) {
            fprintf(stderr, "arith: %s\n", msg);
            free(msg);
            msg = NULL;
        }
        last_status = 1;
        loop_depth--;
        return last_status;
    }

    while (1) {
        err = 0;
        long cond = eval_arith(cmd->arith_cond ? cmd->arith_cond : "1", &err, &msg);
        if (err) { 
            if (msg) {
                fprintf(stderr, "arith: %s\n", msg);
                free(msg);
                msg = NULL;
            }
            last_status = 1; break; }
        if (cond == 0)
            break;

        run_command_list(cmd->body, line);
        if (loop_break) { loop_break--; break; }
        if (loop_continue) {
            if (--loop_continue) {
                loop_depth--;
                return last_status;
            }
            err = 0;
            eval_arith(cmd->arith_update ? cmd->arith_update : "0", &err, &msg);
            if (err) { 
                if (msg) {
                    fprintf(stderr, "arith: %s\n", msg);
                    free(msg);
                    msg = NULL;
                }
                last_status = 1; break; }
            continue;
        }

        err = 0;
        eval_arith(cmd->arith_update ? cmd->arith_update : "0", &err, &msg);
        if (err) { 
            if (msg) {
                fprintf(stderr, "arith: %s\n", msg);
                free(msg);
                msg = NULL;
            }
            last_status = 1; break; }
    }

    loop_depth--;
    return last_status;
}

/*
 * Execute a case statement.  Each pattern list is matched against the
 * supplied word using fnmatch().  The body of the first matching case
 * is executed and last_status from that body is returned.
 */
static int exec_case(Command *cmd, const char *line) {
    int fall = 0;
    for (CaseItem *ci = cmd->cases; ci; ci = ci->next) {
        if (fall) {
            run_command_list(ci->body, line);
            if (!ci->fall_through)
                break;
            fall = ci->fall_through;
            continue;
        }

        int matched = 0;
        for (int i = 0; i < ci->pattern_count; i++) {
            if (fnmatch(ci->patterns[i], cmd->var, 0) == 0) {
                matched = 1;
                break;
            }
        }
        if (matched) {
            run_command_list(ci->body, line);
            if (!ci->fall_through)
                break;
            fall = ci->fall_through;
        }
    }
    return last_status;
}

/*
 * Spawn a subshell running the provided command group.  The parent waits
 * for the child to finish and propagates its exit status.  Returns that
 * status or 1 on fork failure.
 */
static int exec_subshell(Command *cmd, const char *line) {
    pid_t pid = fork();
    if (pid == 0) {
        signal(SIGINT, SIG_DFL);
        run_command_list(cmd->group, line);
        exit(last_status);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status))
            last_status = WEXITSTATUS(status);
        else if (WIFSIGNALED(status))
            last_status = 128 + WTERMSIG(status);
        return last_status;
    } else {
        perror("fork");
        last_status = 1;
        return 1;
    }
}

/*
 * Execute the [[ ... ]] conditional command.  This simply forwards to
 * builtin_cond() and returns its result.
 */
static int exec_cond(Command *cmd, const char *line) {
    (void)line;
    char **args = xcalloc(cmd->word_count + 1, sizeof(char *));
    for (int i = 0; i < cmd->word_count; i++) {
        args[i] = expand_var(cmd->words[i]);
        if (!args[i]) {
            for (int j = 0; j < i; j++)
                free(args[j]);
            free(args);
            last_status = 1;
            return 1;
        }
    }
    args[cmd->word_count] = NULL;
    builtin_cond(args);
    for (int i = 0; i < cmd->word_count; i++)
        free(args[i]);
    free(args);
    return last_status;
}

/*
 * Execute a (( ... )) arithmetic command. The expression is evaluated with
 * eval_arith() and the exit status becomes 0 for a non-zero result or 1
 * otherwise.
 */
static int exec_arith(Command *cmd, const char *line) {
    (void)line;
    int err = 0;
    char *msg = NULL;
    long val = eval_arith(cmd->text ? cmd->text : "0", &err, &msg);
    if (err) {
        if (msg) {
            fprintf(stderr, "arith: %s\n", msg);
            free(msg);
        }
        last_status = 1;
    }
    else
        last_status = (val != 0) ? 0 : 1;
    return last_status;
}

/*
 * Execute a { ... } command group in the current shell.  Simply runs
 * the contained command list and returns its status.
 */
static int exec_group(Command *cmd, const char *line) {
    run_command_list(cmd->group, line);
    return last_status;
}

int run_pipeline(Command *cmd, const char *line) {
    if (!cmd)
        return 0;
    if (opt_hashall && cmd->type == CMD_PIPELINE) {
        for (PipelineSegment *seg = cmd->pipeline; seg; seg = seg->next) {
            if (seg->argv[0] && !strchr(seg->argv[0], '/'))
                hash_add(seg->argv[0]);
        }
    }
    int r = 0;
    switch (cmd->type) {
    case CMD_PIPELINE:
        r = exec_pipeline(cmd, line); break;
    case CMD_FUNCDEF:
        r = exec_funcdef(cmd, line); break;
    case CMD_IF:
        r = exec_if(cmd, line); break;
    case CMD_WHILE:
        r = exec_while(cmd, line); break;
    case CMD_UNTIL:
        r = exec_until(cmd, line); break;
    case CMD_FOR:
        r = exec_for(cmd, line); break;
    case CMD_SELECT:
        r = exec_select(cmd, line); break;
    case CMD_FOR_ARITH:
        r = exec_for_arith(cmd, line); break;
    case CMD_CASE:
        r = exec_case(cmd, line); break;
    case CMD_SUBSHELL:
        r = exec_subshell(cmd, line); break;
    case CMD_COND:
        r = exec_cond(cmd, line); break;
    case CMD_ARITH:
        r = exec_arith(cmd, line); break;
    case CMD_GROUP:
        r = exec_group(cmd, line); break;
    default:
        r = 0; break;
    }
    if (cmd->negate) {
        last_status = (r == 0 ? 1 : 0);
        r = last_status;
    }
    return r;
}

