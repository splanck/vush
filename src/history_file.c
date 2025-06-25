/*
 * vush - a simple UNIX shell
 * Licensed under the BSD 2-Clause Simplified License.
 * Loading and saving the history file.
 */

#define _GNU_SOURCE
#include "history.h"
#include "parser.h" /* for MAX_LINE */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "util.h"
#include "state_paths.h"
#include "error.h"

/* functions from history_list.c */
void history_add_entry(const char *cmd, int save_file);
void history_list_iter(void (*cb)(const char *cmd, void *arg), void *arg);
void history_renumber(void);


/*
 * Append ``cmd`` as a single line to the on-disk history file.  This is used
 * whenever a new command is entered so that the file mirrors the in-memory
 * list.
 */
void history_file_append(const char *cmd) {
    char *path = get_history_file();
    if (!path) {
        fprintf(stderr, "warning: unable to determine history file location\n");
        return;
    }
    FILE *f = fopen(path, "a");
    free(path);
    if (!f)
        return;
    fprintf(f, "%s\n", cmd);
    fclose(f);
}

/* Context used when rewriting the entire history file. */
struct rewrite_ctx { FILE *f; };

/* Callback passed to ``history_list_iter`` that writes each command to ``ctx``. */
static void rewrite_cb(const char *cmd, void *arg) {
    struct rewrite_ctx *ctx = arg;
    fprintf(ctx->f, "%s\n", cmd);
}

/*
 * Rewrite the history file so that it contains exactly the commands stored in
 * memory.  This is called after deletions or when the in-memory limit differs
 * from the file limit.
 */
void history_file_rewrite(void) {
    char *path = get_history_file();
    if (!path) {
        fprintf(stderr, "warning: unable to determine history file location\n");
        return;
    }
    FILE *f = fopen(path, "w");
    free(path);
    if (!f)
        return;
    struct rewrite_ctx ctx = { .f = f };
    history_list_iter(rewrite_cb, &ctx);
    fclose(f);
}

/* Remove all contents from the history file. */
void history_file_clear(void) {
    char *path = get_history_file();
    if (!path) {
        fprintf(stderr, "warning: unable to determine history file location\n");
        return;
    }
    FILE *f = fopen(path, "w");
    free(path);
    if (f)
        fclose(f);
}

/*
 * Read the on-disk history file and populate the in-memory list.  Each line
 * becomes a ``HistEntry``.  After loading the IDs are renumbered and the file
 * rewritten to enforce size limits.
 */
void load_history(void) {
    char *path = get_history_file();
    if (!path) {
        fprintf(stderr, "warning: unable to determine history file location\n");
        return;
    }
    FILE *f = fopen(path, "r");
    free(path);
    if (!f)
        return;
    char line[MAX_LINE];
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        if (len && line[len - 1] == '\n')
            line[len - 1] = '\0';
        history_add_entry(line, 0);
    }
    fclose(f);
    history_renumber();
    history_file_rewrite();
}
