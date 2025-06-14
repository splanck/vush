#define _GNU_SOURCE
#include "builtins.h"
#include "history.h"
#include "parser.h"
#include "execute.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <limits.h>

extern int last_status;

/* Display the command history or modify it with -c to clear or -d N to
 * delete a specific entry. */
int builtin_history(char **args)
{
    if (args[1]) {
        if (strcmp(args[1], "-c") == 0 && !args[2]) {
            clear_history();
            return 1;
        } else if (strcmp(args[1], "-d") == 0 && args[2] && !args[3]) {
            char *end;
            long id = strtol(args[2], &end, 10);
            if (*end || id <= 0) {
                fprintf(stderr, "history: invalid entry\n");
                return 1;
            }
            delete_history_entry((int)id);
            return 1;
        } else {
            fprintf(stderr, "usage: history [-c|-d NUMBER]\n");
            return 1;
        }
    }
    print_history();
    return 1;
}

/* Replay or edit commands from history. */
static char *replace_first(const char *str, const char *old, const char *new)
{
    const char *p = strstr(str, old);
    if (!p)
        return strdup(str);
    size_t pre = (size_t)(p - str);
    size_t oldlen = strlen(old);
    size_t newlen = strlen(new);
    size_t len = pre + newlen + strlen(p + oldlen);
    char *res = malloc(len + 1);
    if (!res)
        return NULL;
    memcpy(res, str, pre);
    memcpy(res + pre, new, newlen);
    strcpy(res + pre + newlen, p + oldlen);
    return res;
}

int builtin_fc(char **args)
{
    int list = 0;
    int nonum = 0;
    int rev = 0;
    int immediate = 0;
    const char *editor = NULL;
    const char *subst = NULL;
    int i = 1;
    for (; args[i] && args[i][0] == '-'; i++) {
        if (strcmp(args[i], "-l") == 0) {
            list = 1;
        } else if (strcmp(args[i], "-n") == 0) {
            nonum = 1;
        } else if (strcmp(args[i], "-r") == 0) {
            rev = 1;
        } else if (strcmp(args[i], "-s") == 0) {
            immediate = 1;
        } else if (strcmp(args[i], "-e") == 0 && args[i+1]) {
            editor = args[i+1];
            i++;
        } else {
            fprintf(stderr,
                    "usage: fc [-lnr] [-e editor] [first [last]] | fc -s [old=new] [command]\n");
            return 1;
        }
    }

    if (immediate && list) {
        fprintf(stderr, "fc: -s cannot be used with -l\n");
        return 1;
    }

    int max_id = 1;
    while (history_get_by_id(max_id))
        max_id++;
    max_id -= 1;
    if (max_id <= 0)
        return 1;

    if (immediate) {
        if (args[i] && strchr(args[i], '=')) {
            subst = args[i];
            i++;
        }
        int id = max_id;
        if (args[i]) {
            id = atoi(args[i]);
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
            char *old = malloc(eq - subst + 1);
            if (!old) {
                perror("malloc");
                free(temp);
                return 1;
            }
            memcpy(old, subst, eq - subst);
            old[eq - subst] = '\0';
            const char *new = eq + 1;
            temp = replace_first(cmd, old, new);
            cmd = temp ? temp : cmd;
            free(old);
        }
        printf("%s\n", cmd);
        char *mutable = strdup(cmd);
        if (!mutable) {
            perror("strdup");
            free(temp);
            return 1;
        }
        Command *cmds = parse_line(mutable);
        if (cmds && cmds->pipeline && cmds->pipeline->argv[0])
            run_command_list(cmds, cmd);
        free_commands(cmds);
        free(mutable);
        free(temp);
        return 1;
    }

    int first_id = max_id;
    int last_id = max_id;
    if (args[i]) {
        first_id = atoi(args[i]);
        if (first_id < 0)
            first_id = max_id + first_id + 1;
        if (args[i+1]) {
            last_id = atoi(args[i+1]);
            if (last_id < 0)
                last_id = max_id + last_id + 1;
        } else {
            last_id = first_id;
        }
    }
    if (first_id <= 0 || last_id <= 0 || first_id > max_id || last_id > max_id) {
        fprintf(stderr, "fc: history range out of bounds\n");
        return 1;
    }

    int start = first_id;
    int end = last_id;
    if (!rev && start > end) {
        int tmp = start; start = end; end = tmp;
    } else if (rev && start < end) {
        int tmp = start; start = end; end = tmp;
    }
    int step = (start <= end) ? 1 : -1;

    if (list) {
        for (int id = start;; id += step) {
            const char *cmd = history_get_by_id(id);
            if (cmd) {
                if (nonum)
                    printf("%s\n", cmd);
                else
                    printf("%d %s\n", id, cmd);
            }
            if (id == end)
                break;
        }
        return 1;
    }

    const char *tmpdir = getenv("TMPDIR");
    if (!tmpdir || !*tmpdir)
        tmpdir = "/tmp";
    char template[PATH_MAX];
    snprintf(template, sizeof(template), "%s/vush_fcXXXXXX", tmpdir);

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
        fclose(f);
        unlink(template);
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
    return 1;
}

