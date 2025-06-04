#include "history.h"
#include "parser.h" /* for MAX_LINE */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct HistEntry {
    int id;
    char cmd[MAX_LINE];
    struct HistEntry *next;
} HistEntry;

static HistEntry *head = NULL;
static HistEntry *tail = NULL;
static int next_id = 1;

void add_history(const char *cmd) {
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
}

void print_history(void) {
    for (HistEntry *e = head; e; e = e->next) {
        printf("%d %s\n", e->id, e->cmd);
    }
}

