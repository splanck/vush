#define _GNU_SOURCE
#include "lineedit.h"
#include "history.h"
#include "parser.h" /* for MAX_LINE */
#include <termios.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static void redraw_line(const char *prompt, const char *buf, int prev_len) {
    int len = strlen(buf);
    printf("\r%s%s", prompt, buf);
    if (prev_len > len) {
        for (int i = len; i < prev_len; i++)
            putchar(' ');
        printf("\r%s%s", prompt, buf);
    }
    fflush(stdout);
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
        } else if (c == 0x04 && len == 0) { /* Ctrl-D */
            len = -1;
            break;
        } else if (c == 0x7f) { /* backspace */
            if (pos > 0) {
                memmove(&buf[pos-1], &buf[pos], len - pos);
                pos--;
                len--;
                printf("\b");
                fwrite(&buf[pos], 1, len - pos, stdout);
                putchar(' ');
                for (int i = 0; i < len - pos + 1; i++)
                    printf("\b");
                fflush(stdout);
            }
        } else if (c == '\033') { /* escape sequences */
            char seq[2];
            if (read(STDIN_FILENO, seq, 2) != 2)
                continue;
            if (seq[0] == '[') {
                if (seq[1] == 'D') { /* left */
                    if (pos > 0) {
                        printf("\b");
                        pos--;
                        fflush(stdout);
                    }
                } else if (seq[1] == 'C') { /* right */
                    if (pos < len) {
                        printf("\x1b[C");
                        pos++;
                        fflush(stdout);
                    }
                } else if (seq[1] == 'A') { /* up */
                    const char *h = history_prev();
                    if (h) {
                        strncpy(buf, h, MAX_LINE - 1);
                        buf[MAX_LINE - 1] = '\0';
                        len = pos = strlen(buf);
                        redraw_line(prompt, buf, disp_len);
                        disp_len = len;
                    }
                } else if (seq[1] == 'B') { /* down */
                    const char *h = history_next();
                    if (h) {
                        strncpy(buf, h, MAX_LINE - 1);
                        buf[MAX_LINE - 1] = '\0';
                        len = pos = strlen(buf);
                    } else {
                        buf[0] = '\0';
                        len = pos = 0;
                    }
                    redraw_line(prompt, buf, disp_len);
                    disp_len = len;
                }
            }
        } else if (c >= 32 && c < 127) { /* printable */
            if (len < MAX_LINE - 1) {
                memmove(&buf[pos+1], &buf[pos], len - pos);
                buf[pos] = c;
                fwrite(&buf[pos], 1, len - pos + 1, stdout);
                pos++;
                len++;
                for (int i = 0; i < len - pos; i++)
                    printf("\b");
                fflush(stdout);
                if (len > disp_len)
                    disp_len = len;
            }
        }
    }

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig);

    if (len < 0)
        return NULL;
    buf[len] = '\0';
    return strdup(buf);
}

