/*
 * Control flow execution helpers.
 *
 * Implements loops, conditionals and other shell control structures.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include "shell_state.h"
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <string.h>
#include <fnmatch.h>

#include "control.h"
#include "execute.h"
#include "builtins.h"
#include "vars.h"
#include "func_exec.h"
#include "arith.h"
#include "util.h"
#include "var_expand.h"


int exec_if(Command *cmd, const char *line) {
    run_command_list(cmd->cond, line);
    if (last_status == 0)
        run_command_list(cmd->body, line);
    else if (cmd->else_part)
        run_command_list(cmd->else_part, line);
    return last_status;
}

int exec_while(Command *cmd, const char *line) {
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

int exec_until(Command *cmd, const char *line) {
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

int exec_for(Command *cmd, const char *line) {
    loop_depth++;
    char *last = NULL;
    for (int i = 0; i < cmd->word_count; i++) {
        char *word = cmd->words[i];
        char *exp = cmd->word_expand ?
                     (cmd->word_expand[i] ? expand_var(word) : strdup(word)) :
                     expand_var(word);
        if (!exp) { loop_depth--; free(last); return last_status; }
        int count = 0;
        char **fields;
        if (cmd->word_quoted && cmd->word_quoted[i]) {
            fields = malloc(2 * sizeof(char *));
            if (!fields) { free(exp); loop_depth--; free(last); return last_status; }
            fields[0] = exp;
            fields[1] = NULL;
            count = 1;
        } else {
            fields = split_fields(exp, &count);
            free(exp);
        }
        for (int fi = 0; fi < count; fi++) {
            char *w = fields[fi];
            if (cmd->var) {
                set_shell_var(cmd->var, w);
                setenv(cmd->var, w, 1);
                free(last);
                last = strdup(w);
                if (!last)
                    perror("strdup");
            }
            run_command_list(cmd->body, line);
            if (loop_break) break;
            if (loop_continue) {
                if (--loop_continue) {
                    for (int fj = fi; fj < count; fj++)
                        free(fields[fj]);
                    free(fields);
                    if (cmd->var && last) {
                        set_shell_var(cmd->var, last);
                        setenv(cmd->var, last, 1);
                    }
                    free(last);
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
    if (cmd->var && last) {
        set_shell_var(cmd->var, last);
        setenv(cmd->var, last, 1);
    }
    free(last);
    loop_depth--;
    return last_status;
}

int exec_select(Command *cmd, const char *line) {
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

int exec_for_arith(Command *cmd, const char *line) {
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

int exec_case(Command *cmd, const char *line) {
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
        for (int i = 0; ci->patterns && ci->patterns[i]; i++) {
            /* ci->patterns is NULL terminated */
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

int exec_subshell(Command *cmd, const char *line) {
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

int exec_cond(Command *cmd, const char *line) {
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

int exec_arith(Command *cmd, const char *line) {
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

int exec_group(Command *cmd, const char *line) {
    run_command_list(cmd->group, line);
    return last_status;
}

