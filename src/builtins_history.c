/*
 * vush - a simple UNIX shell
 * Licensed under the BSD 2-Clause Simplified License.
 * History related builtin commands.
 */

#define _GNU_SOURCE
#include "builtins.h"
#include "history.h"
#include "parser.h"
#include "execute.h"
#include "util.h"

#include "shell_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <limits.h>
#include <ctype.h>
#include <stdbool.h>


/* Display the command history or modify it with -c to clear or -d N to
 * delete a specific entry. */
int builtin_history(char **args)
{
    if (args[1]) {
        if (strcmp(args[1], "-c") == 0 && !args[2]) {
            clear_history();
            return 1;
        } else if (strcmp(args[1], "-d") == 0 && args[2] && !args[3]) {
            int id;
            if (parse_positive_int(args[2], &id) < 0 || id <= 0) {
                fprintf(stderr, "history: invalid entry\n");
                return 1;
            }
            /* remove the entry and renumber remaining history items */
            delete_history_entry(id);
            return 1;
        } else {
            fprintf(stderr, "usage: history [-c|-d NUMBER]\n");
            return 1;
        }
    }
    print_history();
    return 1;
}

/*
 * Replace the first occurrence of ``old`` in ``str`` with ``new`` and return a
 * newly allocated string containing the result.  If ``old`` does not appear,
 * a duplicate of ``str`` is returned.
 */
static char *replace_first(const char *str, const char *old, const char *new)
{
    const char *p = strstr(str, old);
    if (!p)
        return xstrdup(str);
    size_t pre = (size_t)(p - str);
    size_t oldlen = strlen(old);
    size_t newlen = strlen(new);
    size_t len = pre + newlen + strlen(p + oldlen);
    char *res = xmalloc(len + 1);
    memcpy(res, str, pre);
    memcpy(res + pre, new, newlen);
    strcpy(res + pre + newlen, p + oldlen);
    return res;
}

typedef struct {
    int list;
    int nonum;
    int rev;
    int immediate;
    const char *editor;
    int arg_index;
} FcOptions;

/* Parse options for the ``fc`` builtin.  ``opts`` is populated with the parsed
 * flags and the index of the first non-option argument.  Returns 0 on success
 * or -1 on invalid usage.
 */
static int parse_fc_options(char **args, FcOptions *opts) {
    opts->list = 0;
    opts->nonum = 0;
    opts->rev = 0;
    opts->immediate = 0;
    opts->editor = NULL;

    int i = 1;
    for (; args[i] && args[i][0] == '-'; i++) {
        if (args[i][1] && isdigit((unsigned char)args[i][1])) {
            bool all_digits = true;
            for (int j = 1; args[i][j]; j++) {
                if (!isdigit((unsigned char)args[i][j])) {
                    all_digits = false;
                    break;
                }
            }
            if (all_digits)
                break;
        }
        if (strcmp(args[i], "-l") == 0) {
            opts->list = 1;
        } else if (strcmp(args[i], "-n") == 0) {
            opts->nonum = 1;
        } else if (strcmp(args[i], "-r") == 0) {
            opts->rev = 1;
        } else if (strcmp(args[i], "-s") == 0) {
            opts->immediate = 1;
        } else if (strcmp(args[i], "-e") == 0 && args[i + 1]) {
            opts->editor = args[i + 1];
            i++;
        } else {
            fprintf(stderr,
                    "usage: fc [-lnr] [-e editor] [first [last]] | fc -s [old=new] [command]\n");
            return -1;
        }
    }
    opts->arg_index = i;
    return 0;
}

/*
 * Determine the history range specified to ``fc``.  ``idx`` points to the
 * first range argument in ``args`` and ``max_id`` is the last valid history
 * identifier.  ``rev`` indicates whether the range should be reversed.
 * ``start``/``end`` receive the resulting ids and ``step`` the iteration step.
 * Returns 0 on success or -1 on invalid input.
 */
static int get_fc_range(char **args, int idx, int max_id, int rev,
                        int *start, int *end, int *step) {
    int first_id = max_id;
    int last_id = max_id;
    if (args[idx]) {
        first_id = atoi(args[idx]);
        if (first_id < 0)
            first_id = max_id + first_id + 1;
        if (args[idx + 1]) {
            last_id = atoi(args[idx + 1]);
            if (last_id < 0)
                last_id = max_id + last_id + 1;
        } else {
            last_id = first_id;
        }
    }
    if (first_id <= 0 || last_id <= 0 ||
        first_id > max_id || last_id > max_id) {
        fprintf(stderr, "fc: history range out of bounds\n");
        return -1;
    }

    *start = first_id;
    *end = last_id;
    if (!rev && *start > *end) {
        int tmp = *start; *start = *end; *end = tmp;
    } else if (rev && *start < *end) {
        int tmp = *start; *start = *end; *end = tmp;
    }
    *step = (*start <= *end) ? 1 : -1;
    return 0;
}

/*
 * Execute a single history entry immediately as ``fc -s`` would.  Optional
 * substitution of the form ``old=new`` is applied before execution.
 */
static int fc_execute_immediate(char **args, int idx, int max_id) {
    const char *subst = NULL;
    if (args[idx] && strchr(args[idx], '=')) {
        subst = args[idx];
        idx++;
    }
    int id = max_id;
    if (args[idx]) {
        id = atoi(args[idx]);
        if (id < 0)
            id = max_id + id + 1;
    }
    if (id <= 0 || id > max_id) {
        fprintf(stderr, "fc: history range out of bounds\n");
        return 1;
    }

    const char *cmd = history_get_by_id(id);
    char *temp = NULL;
    if (subst) {
        char *eq = strchr(subst, '=');
        if (!eq)
            return 1;
        char *old = xmalloc(eq - subst + 1);
        memcpy(old, subst, eq - subst);
        old[eq - subst] = '\0';
        const char *new = eq + 1;
        temp = replace_first(cmd, old, new);
        cmd = temp ? temp : cmd;
        free(old);
    }
    printf("%s\n", cmd);
    char *mutable = xstrdup(cmd);
    Command *cmds = parse_line(mutable);
    if (cmds && cmds->pipeline && cmds->pipeline->argv[0])
        run_command_list(cmds, cmd);
    free_commands(cmds);
    free(mutable);
    free(temp);
    return 1;
}

/*
 * Write the selected history range to a temporary file, invoke an editor on
 * it and then execute each resulting command.  When ``opts->list`` is set the
 * commands are listed instead of edited.
 */
static int fc_edit_commands(int start, int end, int step, const FcOptions *opts) {
    if (opts->list) {
        for (int id = start;; id += step) {
            const char *cmd = history_get_by_id(id);
            if (cmd) {
                if (opts->nonum)
                    printf("%s\n", cmd);
                else
                    printf("%d %s\n", id, cmd);
            }
            if (id == end)
                break;
        }
        return 1;
    }

    const char *editor = opts->editor;
    const char *tmpdir = getenv("TMPDIR");
    if (!tmpdir || !*tmpdir)
        tmpdir = "/tmp";
    size_t len = strlen(tmpdir) + sizeof("/vush_fcXXXXXX");
    char *template = malloc(len);
    if (!template) {
        perror("malloc");
        return 1;
    }
    snprintf(template, len, "%s/vush_fcXXXXXX", tmpdir);

    int fd = mkstemp(template);
    if (fd < 0) {
        perror("mkstemp");
        return 1;
    }
    FILE *f = fdopen(fd, "w+");
    if (!f) {
        perror("fdopen");
        close(fd);
        unlink(template);
        return 1;
    }
    for (int id = start;; id += step) {
        const char *cmd = history_get_by_id(id);
        if (cmd)
            fprintf(f, "%s\n", cmd);
        if (id == end)
            break;
    }
    fflush(f);

    if (!editor)
        editor = getenv("FCEDIT");
    if (!editor || !*editor)
        editor = "ed";

    pid_t pid = fork();
    if (pid == 0) {
        execlp(editor, editor, template, NULL);
        perror(editor);
        _exit(127);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
    } else {
        perror("fork");
        unlink(template);
        fclose(f);
        return 1;
    }

    fseek(f, 0, SEEK_SET);
    char line[MAX_LINE];
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        if (len && line[len-1] == '\n')
            line[len-1] = '\0';
        Command *cmds = parse_line(line);
        if (cmds && cmds->pipeline && cmds->pipeline->argv[0])
            run_command_list(cmds, line);
        free_commands(cmds);
    }

    fclose(f);
    unlink(template);
    free(template);
    return 1;
}

/* Replay or edit previous commands from the history list. */
int builtin_fc(char **args)
{
    FcOptions opts;
    if (parse_fc_options(args, &opts) < 0)
        return 1;

    if (opts.immediate && opts.list) {
        fprintf(stderr, "fc: -s cannot be used with -l\n");
        return 1;
    }

    int max_id = 1;
    while (history_get_by_id(max_id))
        max_id++;
    max_id -= 1;
    if (max_id <= 0)
        return 1;

    if (opts.immediate)
        return fc_execute_immediate(args, opts.arg_index, max_id);

    int start, end, step;
    if (get_fc_range(args, opts.arg_index, max_id, opts.rev, &start, &end,
                     &step) < 0)
        return 1;

    return fc_edit_commands(start, end, step, &opts);
}

