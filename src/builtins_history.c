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
            int id = atoi(args[2]);
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

/* Replay or edit commands from history. */
int builtin_fc(char **args)
{
    int list = 0;
    const char *editor = NULL;
    int i = 1;
    for (; args[i] && args[i][0] == '-'; i++) {
        if (strcmp(args[i], "-l") == 0) {
            list = 1;
        } else if (strcmp(args[i], "-e") == 0 && args[i+1]) {
            editor = args[i+1];
            i++;
        } else {
            fprintf(stderr, "usage: fc [-l] [-e editor] [first [last]]\n");
            return 1;
        }
    }

    int max_id = 1;
    while (history_get_by_id(max_id))
        max_id++;
    max_id -= 1;
    if (max_id <= 0)
        return 1;

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
    if (first_id > last_id) {
        int tmp = first_id;
        first_id = last_id;
        last_id = tmp;
    }

    if (list) {
        for (int id = first_id; id <= last_id; id++) {
            const char *cmd = history_get_by_id(id);
            if (cmd)
                printf("%d %s\n", id, cmd);
        }
        return 1;
    }

    char tmpname[] = "/tmp/vush_fcXXXXXX";
    int fd = mkstemp(tmpname);
    if (fd < 0) {
        perror("mkstemp");
        return 1;
    }
    FILE *f = fdopen(fd, "w+");
    if (!f) {
        perror("fdopen");
        close(fd);
        unlink(tmpname);
        return 1;
    }
    for (int id = first_id; id <= last_id; id++) {
        const char *cmd = history_get_by_id(id);
        if (cmd)
            fprintf(f, "%s\n", cmd);
    }
    fflush(f);

    if (!editor)
        editor = getenv("FCEDIT");
    if (!editor || !*editor)
        editor = "ed";

    pid_t pid = fork();
    if (pid == 0) {
        execlp(editor, editor, tmpname, NULL);
        perror(editor);
        _exit(127);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
    } else {
        perror("fork");
        fclose(f);
        unlink(tmpname);
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
    unlink(tmpname);
    return 1;
}

