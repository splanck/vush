/*
 * Execution engine handling pipelines and control flow.
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

#include "execute.h"
#include "jobs.h"
#include "builtins.h"
#include "scriptargs.h"
#include "options.h"
#include "pipeline.h"
#include "func_exec.h"

extern int last_status;

int loop_break = 0;
int loop_continue = 0;

int run_command_list(Command *cmds, const char *line);
static int apply_temp_assignments(PipelineSegment *pipeline);
void setup_redirections(PipelineSegment *seg);
static int spawn_pipeline_segments(PipelineSegment *pipeline, int background,
                                   const char *line);
/*
 * Apply temporary variable assignments before running a pipeline.
 * Builtins and functions are executed directly while environment
 * variables are preserved and restored around the call.
 */

static int apply_temp_assignments(PipelineSegment *pipeline) {
    if (pipeline->next)
        return 0;

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
                char *body = strndup(val+1, vlen-2);
                char *p = body;
                char **vals = NULL; int count=0;
                while (*p) {
                    while (*p==' '||*p=='\t') p++;
                    if (*p=='\0') break;
                    char *start=p;
                    while (*p && *p!=' ' && *p!='\t') p++;
                    if (*p) *p++='\0';
                    vals = realloc(vals,sizeof(char*)*(count+1));
                    vals[count++] = strdup(start);
                }
                set_shell_array(name, vals, count);
                for(int j=0;j<count;j++) free(vals[j]);
                free(vals);
                free(body);
            } else {
                set_shell_var(name, val);
            }
            free(name);
        }
        last_status = 0;
        return 1;
    }

    if (!pipeline->argv[0])
        return 0;

    struct {
        char *name;
        char *env;
        char *var;
        int had_env;
        int had_var;
    } backs[pipeline->assign_count];

    for (int i = 0; i < pipeline->assign_count; i++) {
        char *eq = strchr(pipeline->assigns[i], '=');
        if (!eq) {
            backs[i].name = NULL;
            continue;
        }
        backs[i].name = strndup(pipeline->assigns[i], eq - pipeline->assigns[i]);
        const char *oe = getenv(backs[i].name);
        backs[i].had_env = oe != NULL;
        backs[i].env = oe ? strdup(oe) : NULL;
        const char *ov = get_shell_var(backs[i].name);
        backs[i].had_var = ov != NULL;
        backs[i].var = ov ? strdup(ov) : NULL;
        char *val = eq + 1;
        size_t vlen = strlen(val);
        if (vlen > 1 && val[0] == '(' && val[vlen-1] == ')') {
            char *body = strndup(val+1, vlen-2);
            char *p = body;
            char **vals = NULL; int count=0;
            size_t joinlen = 0;
            while (*p) {
                while (*p==' '||*p=='\t') p++;
                if (*p=='\0') break;
                char *start=p;
                while (*p && *p!=' ' && *p!='\t') p++;
                if (*p) *p++='\0';
                vals = realloc(vals,sizeof(char*)*(count+1));
                vals[count++] = strdup(start);
                joinlen += strlen(start) + 1;
            }
            char *joined = malloc(joinlen+1);
            if (joined) {
                joined[0] = '\0';
                for (int j=0;j<count;j++) {
                    strcat(joined, vals[j]);
                    if (j < count-1) strcat(joined, " ");
                }
            }
            setenv(backs[i].name, joined ? joined : "", 1);
            set_shell_array(backs[i].name, vals, count);
            free(joined);
            for(int j=0;j<count;j++) free(vals[j]);
            free(vals);
            free(body);
        } else {
            setenv(backs[i].name, val, 1);
            set_shell_var(backs[i].name, val);
        }
    }

    int handled = 0;
    if (run_builtin(pipeline->argv))
        handled = 1;
    else {
        Command *fn = get_function(pipeline->argv[0]);
        if (fn) {
            run_function(fn, pipeline->argv);
            handled = 1;
        }
    }

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
        dup2(fd, STDIN_FILENO);
        close(fd);
    }

    if (seg->out_file && seg->err_file && strcmp(seg->out_file, seg->err_file) == 0 &&
        seg->append == seg->err_append) {
        int flags = O_WRONLY | O_CREAT | (seg->append ? O_APPEND : O_TRUNC);
        if (opt_noclobber && !seg->append)
            flags |= O_EXCL;
        int fd = open(seg->out_file, flags, 0644);
        if (fd < 0) {
            perror(seg->out_file);
            exit(1);
        }
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        close(fd);
    } else {
        if (seg->out_file) {
            int flags = O_WRONLY | O_CREAT | (seg->append ? O_APPEND : O_TRUNC);
            if (opt_noclobber && !seg->append)
                flags |= O_EXCL;
            int fd = open(seg->out_file, flags, 0644);
            if (fd < 0) {
                perror(seg->out_file);
                exit(1);
            }
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }
        if (seg->err_file) {
            int flags = O_WRONLY | O_CREAT | (seg->err_append ? O_APPEND : O_TRUNC);
            if (opt_noclobber && !seg->err_append)
                flags |= O_EXCL;
            int fd = open(seg->err_file, flags, 0644);
            if (fd < 0) {
                perror(seg->err_file);
                exit(1);
            }
            dup2(fd, STDERR_FILENO);
            close(fd);
        }
    }

    if (seg->dup_out != -1)
        dup2(seg->dup_out, STDOUT_FILENO);
    if (seg->dup_err != -1)
        dup2(seg->dup_err, STDERR_FILENO);
}


/*
 * Fork and execute each segment of a pipeline, wiring up pipes
 * between processes and waiting as needed.
 */
static int spawn_pipeline_segments(PipelineSegment *pipeline, int background,
                                   const char *line) {
    int seg_count = 0;
    for (PipelineSegment *tmp = pipeline; tmp; tmp = tmp->next)
        seg_count++;
    pid_t *pids = calloc(seg_count, sizeof(pid_t));
    if (!pids)
        return 1;

    int i = 0;
    int in_fd = -1;
    for (PipelineSegment *seg = pipeline; seg; seg = seg->next) {
        pid_t pid = fork_segment(seg, &in_fd);
        if (pid < 0) {
            free(pids);
            last_status = 1;
            return 1;
        }
        pids[i++] = pid;
    }

    if (in_fd != -1)
        close(in_fd);

    wait_for_pipeline(pids, i, background, line);
    free(pids);
    return last_status;
}

static int run_pipeline_internal(PipelineSegment *pipeline, int background, const char *line) {
    if (!pipeline)
        return 0;

    if (opt_xtrace && line)
        fprintf(stderr, "+ %s\n", line);

    if (apply_temp_assignments(pipeline)) {
        cleanup_proc_subs();
        return last_status;
    }

    if (!pipeline->argv[0] || pipeline->argv[0][0] == '\0') {
        fprintf(stderr, "syntax error: missing command\n");
        last_status = 1;
        cleanup_proc_subs();
        return last_status;
    }
    int r = spawn_pipeline_segments(pipeline, background, line);
    cleanup_proc_subs();
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
        if (run)
            run_pipeline(c, line);
        prev = c->op;
        if (func_return || loop_break || loop_continue)
            break;
    }
    return last_status;
}

int run_pipeline(Command *cmd, const char *line) {
    if (!cmd)
        return 0;

    switch (cmd->type) {
    case CMD_PIPELINE:
        return run_pipeline_internal(cmd->pipeline, cmd->background, line);
    case CMD_FUNCDEF:
        define_function(cmd->var, cmd->body, cmd->text);
        cmd->body = NULL;
        return last_status;
    case CMD_IF:
        run_command_list(cmd->cond, line);
        if (last_status == 0)
            run_command_list(cmd->body, line);
        else if (cmd->else_part)
            run_command_list(cmd->else_part, line);
        return last_status;
    case CMD_WHILE:
        while (1) {
            run_command_list(cmd->cond, line);
            if (loop_break) { loop_break = 0; break; }
            if (loop_continue) { loop_continue = 0; continue; }
            if (last_status != 0)
                break;
            run_command_list(cmd->body, line);
            if (loop_break) { loop_break = 0; break; }
            if (loop_continue) { loop_continue = 0; continue; }
        }
        return last_status;
    case CMD_UNTIL:
        while (1) {
            run_command_list(cmd->cond, line);
            if (loop_break) { loop_break = 0; break; }
            if (loop_continue) { loop_continue = 0; continue; }
            if (last_status == 0)
                break;
            run_command_list(cmd->body, line);
            if (loop_break) { loop_break = 0; break; }
            if (loop_continue) { loop_continue = 0; continue; }
        }
        return last_status;
    case CMD_FOR:
        for (int i = 0; i < cmd->word_count; i++) {
            if (cmd->var)
                setenv(cmd->var, cmd->words[i], 1);
            run_command_list(cmd->body, line);
            if (loop_break) { loop_break = 0; break; }
            if (loop_continue) { loop_continue = 0; continue; }
        }
        return last_status;
    case CMD_SELECT: {
        char input[MAX_LINE];
        while (1) {
            for (int i = 0; i < cmd->word_count; i++)
                printf("%d) %s\n", i + 1, cmd->words[i]);
            fputs("? ", stdout);
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
            if (loop_break) { loop_break = 0; break; }
            if (loop_continue) { loop_continue = 0; continue; }
        }
        return last_status;
    }
    case CMD_FOR_ARITH:
        eval_arith(cmd->arith_init ? cmd->arith_init : "0");
        while (1) {
            long cond = eval_arith(cmd->arith_cond ? cmd->arith_cond : "1");
            if (cond == 0)
                break;
            run_command_list(cmd->body, line);
            if (loop_break) { loop_break = 0; break; }
            eval_arith(cmd->arith_update ? cmd->arith_update : "0");
            if (loop_continue) { loop_continue = 0; continue; }
        }
        return last_status;
    case CMD_CASE:
        for (CaseItem *ci = cmd->cases; ci; ci = ci->next) {
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
            }
        }
        return last_status;
    case CMD_SUBSHELL: {
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
    case CMD_COND:
        builtin_cond(cmd->words);
        return last_status;
    case CMD_GROUP:
        run_command_list(cmd->group, line);
        return last_status;
    default:
        return 0;
    }
}

