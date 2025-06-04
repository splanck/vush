#include "history.h"
#include "parser.h" /* for MAX_LINE */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef struct HistEntry {
    int id;
    char cmd[MAX_LINE];
    struct HistEntry *next;
} HistEntry;

static HistEntry *head = NULL;
static HistEntry *tail = NULL;
static int next_id = 1;

static void add_history_entry(const char *cmd, int save_file) {
    HistEntry *e = malloc(sizeof(HistEntry));
    if (!e) return;
    e->id = next_id++;
    strncpy(e->cmd, cmd, MAX_LINE - 1);
    e->cmd[MAX_LINE - 1] = '\0';
    e->next = NULL;
    if (!head) {
        head = tail = e;
    } else {
        tail->next = e;
        tail = e;
    }

    if (save_file) {
        const char *home = getenv("HOME");
        if (home) {
            char path[PATH_MAX];
            snprintf(path, sizeof(path), "%s/.vush_history", home);
            FILE *f = fopen(path, "a");
            if (f) {
                fprintf(f, "%s\n", cmd);
                fclose(f);
            }
        }
    }
}

void add_history(const char *cmd) {
    add_history_entry(cmd, 1);
}

void print_history(void) {
    for (HistEntry *e = head; e; e = e->next) {
        printf("%d %s\n", e->id, e->cmd);
    }
}

void load_history(void) {
    const char *home = getenv("HOME");
    if (!home)
        return;
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/.vush_history", home);
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    char line[MAX_LINE];
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        if (len && line[len - 1] == '\n')
            line[len - 1] = '\0';
        add_history_entry(line, 0);
    }
    fclose(f);
}

