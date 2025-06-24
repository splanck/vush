/*
 * vush - a simple UNIX shell
 * Licensed under the BSD 2-Clause Simplified License.
 * Incremental history search routines.
 */

/*
 * Incremental history search used by the line editor.
 */
#include "history_search.h"
#include "history.h"
#include "parser.h" /* for MAX_LINE */
#include <termios.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

/* helper to redraw the search prompt */
static int redraw_search(const char *label, const char *search,
                         const char *match, int prev_len) {
    char line[MAX_LINE * 2];
    int len = snprintf(line, sizeof(line), "(%s)`%s`: %s",
                        label, search, match ? match : "");
    printf("\r%s", line);
    if (prev_len > len) {
        for (int i = len; i < prev_len; i++)
            putchar(' ');
        printf("\r%s", line);
    }
    fflush(stdout);
    return len;
}

int reverse_search(const char *prompt, char *buf, int *lenp, int *posp,
                   int *disp_lenp) {
    char search[MAX_LINE];
    int s_len = 0;
    search[0] = '\0';
    char saved[MAX_LINE];
    int saved_len = *lenp;
    int saved_pos = *posp;
    strncpy(saved, buf, MAX_LINE - 1);
    saved[MAX_LINE - 1] = '\0';
    const char *match = NULL;
    history_reset_search();
    int disp = 0;

    while (1) {
        disp = redraw_search("reverse-i-search", search, match, disp);
        char c;
        if (read(STDIN_FILENO, &c, 1) != 1)
            return -1;
        if (c == 0x07 || c == '\033') { /* Ctrl-G or Esc cancel */
            strncpy(buf, saved, MAX_LINE - 1);
            buf[MAX_LINE - 1] = '\0';
            *lenp = saved_len;
            *posp = saved_pos;
            printf("\r%s%s", prompt, buf);
            if (*lenp > *disp_lenp)
                *disp_lenp = *lenp;
            history_reset_search();
            return 0;
        } else if (c == 0x12) { /* Ctrl-R cycle */
            const char *h = history_search_prev(search);
            if (h)
                match = h;
        } else if (c == 0x7f) { /* backspace */
            if (s_len > 0) {
                search[--s_len] = '\0';
                history_reset_search();
                match = history_search_prev(search);
            }
        } else if (c == '\r' || c == '\n') {
            if (match) {
                strncpy(buf, match, MAX_LINE - 1);
                buf[MAX_LINE - 1] = '\0';
                *lenp = *posp = strlen(buf);
                *disp_lenp = *lenp;
                printf("\r%s%s\n", prompt, buf);
            } else {
                printf("\r\n");
                *lenp = *posp = 0;
                buf[0] = '\0';
            }
            history_reset_search();
            return 1;
        } else if (c >= 32 && c < 127) {
            if (s_len < MAX_LINE - 1) {
                search[s_len++] = c;
                search[s_len] = '\0';
                history_reset_search();
                match = history_search_prev(search);
            }
        }
    }
}

int forward_search(const char *prompt, char *buf, int *lenp, int *posp,
                   int *disp_lenp) {
    char search[MAX_LINE];
    int s_len = 0;
    search[0] = '\0';
    char saved[MAX_LINE];
    int saved_len = *lenp;
    int saved_pos = *posp;
    strncpy(saved, buf, MAX_LINE - 1);
    saved[MAX_LINE - 1] = '\0';
    const char *match = NULL;
    history_reset_search();
    int disp = 0;

    while (1) {
        disp = redraw_search("forward-i-search", search, match, disp);
        char c;
        if (read(STDIN_FILENO, &c, 1) != 1)
            return -1;
        if (c == 0x07 || c == '\033') {
            strncpy(buf, saved, MAX_LINE - 1);
            buf[MAX_LINE - 1] = '\0';
            *lenp = saved_len;
            *posp = saved_pos;
            printf("\r%s%s", prompt, buf);
            if (*lenp > *disp_lenp)
                *disp_lenp = *lenp;
            history_reset_search();
            return 0;
        } else if (c == 0x13) { /* Ctrl-S cycle */
            const char *h = history_search_next(search);
            if (h)
                match = h;
        } else if (c == 0x7f) {
            if (s_len > 0) {
                search[--s_len] = '\0';
                history_reset_search();
                match = history_search_next(search);
            }
        } else if (c == '\r' || c == '\n') {
            if (match) {
                strncpy(buf, match, MAX_LINE - 1);
                buf[MAX_LINE - 1] = '\0';
                *lenp = *posp = strlen(buf);
                *disp_lenp = *lenp;
                printf("\r%s%s\n", prompt, buf);
            } else {
                printf("\r\n");
                *lenp = *posp = 0;
                buf[0] = '\0';
            }
            history_reset_search();
            return 1;
        } else if (c >= 32 && c < 127) {
            if (s_len < MAX_LINE - 1) {
                search[s_len++] = c;
                search[s_len] = '\0';
                history_reset_search();
                match = history_search_next(search);
            }
        }
    }
}

int handle_history_search(char c, const char *prompt, char *buf,
                          int *lenp, int *posp, int *disp_lenp) {
    if (c == 0x12)
        return reverse_search(prompt, buf, lenp, posp, disp_lenp);
    else if (c == 0x13)
        return forward_search(prompt, buf, lenp, posp, disp_lenp);
    return 0;
}

