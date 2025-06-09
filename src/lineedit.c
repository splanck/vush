#define _GNU_SOURCE
#include "lineedit.h"
#include "history.h"
#include "parser.h" /* for MAX_LINE */
#include <termios.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "completion.h"
#include "history_search.h"

static void redraw_line(const char *prompt, const char *buf, int prev_len,
                        int pos);

static void handle_backspace(char *buf, int *lenp, int *posp);
static void handle_insert(char *buf, int *lenp, int *posp, char c,
                          int *disp_lenp);
static void handle_word_erase(const char *prompt, char *buf, int *lenp,
                              int *posp, int *disp_lenp);
static void handle_line_start_erase(const char *prompt, char *buf, int *lenp,
                                    int *posp, int *disp_lenp);
static void handle_line_end_erase(const char *prompt, char *buf, int *lenp,
                                  int *posp, int *disp_lenp);
static int handle_ctrl_commands(char c, const char *prompt, char *buf,
                                int *lenp, int *posp, int *disp_lenp);
static void handle_arrow_keys(const char *prompt, char *buf, int *lenp,
                              int *posp, int *disp_lenp);


static void redraw_line(const char *prompt, const char *buf, int prev_len, int pos) {
    int len = strlen(buf);
    printf("\r%s%s", prompt, buf);
    if (prev_len > len) {
        for (int i = len; i < prev_len; i++)
            putchar(' ');
        printf("\r%s%s", prompt, buf);
    }
    for (int i = len; i > pos; i--)
        printf("\b");
    fflush(stdout);
}

static void handle_backspace(char *buf, int *lenp, int *posp) {
    if (*posp > 0) {
        memmove(&buf[*posp - 1], &buf[*posp], *lenp - *posp);
        (*posp)--;
        (*lenp)--;
        printf("\b");
        fwrite(&buf[*posp], 1, *lenp - *posp, stdout);
        putchar(' ');
        for (int i = 0; i < *lenp - *posp + 1; i++)
            printf("\b");
        fflush(stdout);
    }
}

static void handle_line_start_erase(const char *prompt, char *buf, int *lenp,
                                    int *posp, int *disp_lenp) {
    if (*posp > 0) {
        memmove(buf, &buf[*posp], *lenp - *posp);
        *lenp -= *posp;
        *posp = 0;
        redraw_line(prompt, buf, *disp_lenp, *posp);
        *disp_lenp = *lenp;
    }
}

static void handle_word_erase(const char *prompt, char *buf, int *lenp, int *posp,
                              int *disp_lenp) {
    if (*posp > 0) {
        int end = *posp;
        while (*posp > 0 && (buf[*posp - 1] == ' ' || buf[*posp - 1] == '\t'))
            (*posp)--;
        while (*posp > 0 && buf[*posp - 1] != ' ' && buf[*posp - 1] != '\t')
            (*posp)--;
        memmove(&buf[*posp], &buf[end], *lenp - end);
        *lenp -= end - *posp;
        redraw_line(prompt, buf, *disp_lenp, *posp);
        if (*lenp > *disp_lenp)
            *disp_lenp = *lenp;
    }
}

static void handle_line_end_erase(const char *prompt, char *buf, int *lenp,
                                  int *posp, int *disp_lenp) {
    if (*posp < *lenp) {
        buf[*posp] = '\0';
        *lenp = *posp;
        redraw_line(prompt, buf, *disp_lenp, *posp);
        *disp_lenp = *lenp;
    }
}


static void handle_insert(char *buf, int *lenp, int *posp, char c,
                          int *disp_lenp) {
    if (*lenp < MAX_LINE - 1) {
        memmove(&buf[*posp + 1], &buf[*posp], *lenp - *posp);
        buf[*posp] = c;
        fwrite(&buf[*posp], 1, *lenp - *posp + 1, stdout);
        (*posp)++;
        (*lenp)++;
        for (int i = 0; i < *lenp - *posp; i++)
            printf("\b");
        fflush(stdout);
        if (*lenp > *disp_lenp)
            *disp_lenp = *lenp;
    }
}

static int handle_ctrl_commands(char c, const char *prompt, char *buf,
                                int *lenp, int *posp, int *disp_lenp) {
    switch (c) {
    case 0x7f: /* backspace */
        handle_backspace(buf, lenp, posp);
        return 1;
    case 0x01: /* Ctrl-A */
        while (*posp > 0) {
            printf("\b");
            (*posp)--;
        }
        fflush(stdout);
        return 1;
    case 0x05: /* Ctrl-E */
        while (*posp < *lenp) {
            printf("\x1b[C");
            (*posp)++;
        }
        fflush(stdout);
        return 1;
    case 0x15: /* Ctrl-U */
        handle_line_start_erase(prompt, buf, lenp, posp, disp_lenp);
        return 1;
    case 0x17: /* Ctrl-W */
        handle_word_erase(prompt, buf, lenp, posp, disp_lenp);
        return 1;
    case 0x0b: /* Ctrl-K */
        handle_line_end_erase(prompt, buf, lenp, posp, disp_lenp);
        return 1;
    case 0x0c: /* Ctrl-L */
        printf("\x1b[H\x1b[2J");
        redraw_line(prompt, buf, *disp_lenp, *posp);
        fflush(stdout);
        return 1;
    default:
        return 0;
    }
}

static void handle_arrow_keys(const char *prompt, char *buf, int *lenp,
                              int *posp, int *disp_lenp) {
    char seq[3];
    if (read(STDIN_FILENO, seq, 2) != 2)
        return;
    if (seq[0] != '[')
        return;
    if (seq[1] == 'D') { /* left */
        if (*posp > 0) {
            printf("\b");
            (*posp)--;
            fflush(stdout);
        }
    } else if (seq[1] == 'C') { /* right */
        if (*posp < *lenp) {
            printf("\x1b[C");
            (*posp)++;
            fflush(stdout);
        }
    } else if (seq[1] == 'A') { /* up */
        const char *h = history_prev();
        if (h) {
            strncpy(buf, h, MAX_LINE - 1);
            buf[MAX_LINE - 1] = '\0';
            *lenp = *posp = strlen(buf);
            redraw_line(prompt, buf, *disp_lenp, *posp);
            *disp_lenp = *lenp;
        }
    } else if (seq[1] == 'B') { /* down */
        const char *h = history_next();
        if (h) {
            strncpy(buf, h, MAX_LINE - 1);
            buf[MAX_LINE - 1] = '\0';
            *lenp = *posp = strlen(buf);
        } else {
            buf[0] = '\0';
            *lenp = *posp = 0;
        }
        redraw_line(prompt, buf, *disp_lenp, *posp);
        *disp_lenp = *lenp;
    } else if (seq[1] >= '0' && seq[1] <= '9') {
        if (read(STDIN_FILENO, &seq[2], 1) != 1)
            return;
        if (seq[1] == '1' && seq[2] == '~') { /* Home */
            while (*posp > 0) {
                printf("\b");
                (*posp)--;
            }
            fflush(stdout);
        } else if (seq[1] == '4' && seq[2] == '~') { /* End */
            while (*posp < *lenp) {
                printf("\x1b[C");
                (*posp)++;
            }
            fflush(stdout);
        }
    }
}


char *line_edit(const char *prompt) {
    struct termios orig, raw;
    if (tcgetattr(STDIN_FILENO, &orig) == -1)
        return NULL;
    raw = orig;
    cfmakeraw(&raw);
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        return NULL;

    fputs(prompt, stdout);
    fflush(stdout);

    char buf[MAX_LINE];
    int len = 0;
    int pos = 0;
    int disp_len = 0;

    while (1) {
        char c;
        if (read(STDIN_FILENO, &c, 1) != 1) {
            len = -1;
            break;
        }

        if (c == '\r' || c == '\n') {
            write(STDOUT_FILENO, "\r\n", 2);
            break;
        }

        if (c == 0x04 && len == 0) { /* Ctrl-D */
            len = -1;
            break;
        }

        if (handle_ctrl_commands(c, prompt, buf, &len, &pos, &disp_len))
            continue;

        int hs = handle_history_search(c, prompt, buf, &len, &pos, &disp_len);
        if (hs < 0) {
            len = -1;
            break;
        } else if (hs > 0) {
            break;
        } else if (c == '\t') {
            handle_completion(prompt, buf, &len, &pos, &disp_len);
        } else if (c == '\033') {
            handle_arrow_keys(prompt, buf, &len, &pos, &disp_len);
        } else if (c >= 32 && c < 127) {
            handle_insert(buf, &len, &pos, c, &disp_len);
        }
    }

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig);

    if (len < 0)
        return NULL;
    buf[len] = '\0';
    return strdup(buf);
}

