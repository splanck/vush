#define _GNU_SOURCE
#include "history.h"
#include "parser.h" /* for MAX_LINE */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "util.h"
#include "error.h"

/* functions from history_list.c */
void history_add_entry(const char *cmd, int save_file);
void history_list_iter(void (*cb)(const char *cmd, void *arg), void *arg);
void history_renumber(void);

/* Determine the path to the history file. */
static char *histfile_path(void) {
    return make_user_path("VUSH_HISTFILE", "HISTFILE", ".vush_history");
}

/* Append CMD to the history file. */
void history_file_append(const char *cmd) {
    char *path = histfile_path();
    if (!path)
        return;
    FILE *f = fopen(path, "a");
    free(path);
    if (!f)
        return;
    fprintf(f, "%s\n", cmd);
    fclose(f);
}

struct rewrite_ctx { FILE *f; };
static void rewrite_cb(const char *cmd, void *arg) {
    struct rewrite_ctx *ctx = arg;
    fprintf(ctx->f, "%s\n", cmd);
}

/* Rewrite the entire history file from the current list. */
void history_file_rewrite(void) {
    char *path = histfile_path();
    if (!path)
        return;
    FILE *f = fopen(path, "w");
    free(path);
    if (!f)
        return;
    struct rewrite_ctx ctx = { .f = f };
    history_list_iter(rewrite_cb, &ctx);
    fclose(f);
}

/* Truncate the history file. */
void history_file_clear(void) {
    char *path = histfile_path();
    if (!path)
        return;
    FILE *f = fopen(path, "w");
    free(path);
    if (f)
        fclose(f);
}

/* Load history entries from the history file. */
void load_history(void) {
    char *path = histfile_path();
    if (!path)
        return;
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
