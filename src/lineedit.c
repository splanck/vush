/*
 * vush - a simple UNIX shell
 * Licensed under the BSD 2-Clause Simplified License.
 * Interactive line editor.
 */

/*
 * Interactive line editor for the shell prompt.
 * Input is read in raw terminal mode so keypresses are delivered
 * immediately, allowing the editor to interpret arrow keys, history
 * search and completion without the usual line buffering.  Various
 * control sequences update the buffer and cursor position.
 */
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

enum lineedit_mode lineedit_mode = LINEEDIT_EMACS;

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
static int process_keypress(char c, const char *prompt, char *buf,
                            int *lenp, int *posp, int *disp_lenp);
static char *read_raw_line(const char *prompt);
static char *read_simple_line(const char *prompt);


/* Redraw the prompt and buffer after edits, leaving the cursor at pos. */
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

/* Remove the character before the cursor. */
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

/* Erase from the start of the line up to the cursor. */
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

/* Erase the word immediately before the cursor. */
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

/* Erase from the cursor to the end of the line. */
static void handle_line_end_erase(const char *prompt, char *buf, int *lenp,
                                  int *posp, int *disp_lenp) {
    if (*posp < *lenp) {
        buf[*posp] = '\0';
        *lenp = *posp;
        redraw_line(prompt, buf, *disp_lenp, *posp);
        *disp_lenp = *lenp;
    }
}


/* Insert character c at the cursor position. */
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

/* Handle Ctrl-based editing commands.  Returns 1 if handled. */
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

/* Interpret escape sequences for arrow and home/end keys. */
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

/*
 * Process a single input character and update the editing state.
 * Returns 1 when the line is complete or should be aborted.
 */
static int process_keypress(char c, const char *prompt, char *buf,
                            int *lenp, int *posp, int *disp_lenp) {
    if (c == '\r' || c == '\n') {
        write(STDOUT_FILENO, "\r\n", 2);
        return 1;
    }

    if (c == 0x04 && *lenp == 0) { /* Ctrl-D */
        *lenp = -1;
        return 1;
    }

    if (handle_ctrl_commands(c, prompt, buf, lenp, posp, disp_lenp))
        return 0;

    int hs = handle_history_search(c, prompt, buf, lenp, posp, disp_lenp);
    if (hs < 0) {
        *lenp = -1;
        return 1;
    } else if (hs > 0) {
        return 1;
    } else if (c == '\t') {
        handle_completion(prompt, buf, lenp, posp, disp_lenp);
    } else if (c == '\033') {
        handle_arrow_keys(prompt, buf, lenp, posp, disp_lenp);
    } else if (c >= 32 && c < 127) {
        handle_insert(buf, lenp, posp, c, disp_lenp);
    }
    return 0;
}

/*
 * Read characters in raw mode until Enter is pressed, building the line.
 */
static char *read_raw_line(const char *prompt) {
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

        if (process_keypress(c, prompt, buf, &len, &pos, &disp_len))
            break;
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &orig);

    if (len < 0)
        return NULL;
    buf[len] = '\0';
    return strdup(buf);
}

static char *read_simple_line(const char *prompt) {
    fputs(prompt, stdout);
    fflush(stdout);
    char buf[MAX_LINE];
    if (!fgets(buf, sizeof(buf), stdin))
        return NULL;
    size_t len = strlen(buf);
    if (len && buf[len - 1] == '\n')
        buf[len - 1] = '\0';
    return strdup(buf);
}

/* Read a line using the editor and return it as a new string. */
/*
 * Display PROMPT and read a line using the active editing mode.
 * The returned string must be freed by the caller.
 */
char *line_edit(const char *prompt) {
    if (lineedit_mode == LINEEDIT_VI)
        return read_simple_line(prompt);
    return read_raw_line(prompt);
}

