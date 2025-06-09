#define _GNU_SOURCE
#include "lineedit.h"
#include "history.h"
#include "parser.h" /* for MAX_LINE */
#include <termios.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include "builtins.h"

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
static void handle_completion(const char *prompt, char *buf, int *lenp,
                              int *posp, int *disp_lenp);

static int cmpstr(const void *a, const void *b) {
    const char *aa = *(const char **)a;
    const char *bb = *(const char **)b;
    return strcmp(aa, bb);
}

static int redraw_search(const char *label, const char *search, const char *match, int prev_len) {
    char line[MAX_LINE * 2];
    int len = snprintf(line, sizeof(line), "(%s)`%s`: %s", label, search,
                        match ? match : "");
    printf("\r%s", line);
    if (prev_len > len) {
        for (int i = len; i < prev_len; i++)
            putchar(' ');
        printf("\r%s", line);
    }
    fflush(stdout);
    return len;
}

static int reverse_search(const char *prompt, char *buf, int *lenp, int *posp,
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
            redraw_line(prompt, buf, *disp_lenp, *posp);
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

static int forward_search(const char *prompt, char *buf, int *lenp, int *posp,
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
            redraw_line(prompt, buf, *disp_lenp, *posp);
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

static void handle_completion(const char *prompt, char *buf, int *lenp,
                              int *posp, int *disp_lenp) {
    int start = *posp;
    while (start > 0 && buf[start - 1] != ' ' && buf[start - 1] != '\t')
        start--;

    char prefix[MAX_LINE];
    memcpy(prefix, &buf[start], *posp - start);
    prefix[*posp - start] = '\0';

    int mcount = 0;
    int mcap = 16;
    char **matches = malloc(mcap * sizeof(char *));
    if (!matches)
        matches = NULL;
    const char **bn = get_builtin_names();
    if (matches)
    for (int i = 0; bn[i]; i++) {
        if (strncmp(bn[i], prefix, *posp - start) == 0) {
            if (mcount == mcap) {
                mcap *= 2;
                matches = realloc(matches, mcap * sizeof(char *));
                if (!matches) break;
            }
            matches[mcount++] = strdup(bn[i]);
        }
    }
    DIR *d = opendir(".");
    if (d && matches) {
        struct dirent *de;
        while ((de = readdir(d))) {
            if (strncmp(de->d_name, prefix, *posp - start) == 0) {
                if (mcount == mcap) {
                    mcap *= 2;
                    matches = realloc(matches, mcap * sizeof(char *));
                    if (!matches) break;
                }
                matches[mcount++] = strdup(de->d_name);
            }
        }
        closedir(d);
    }
    if (matches) {
        if (mcount == 1) {
            const char *match = matches[0];
            int mlen = strlen(match);
            if (*lenp + mlen - (*posp - start) < MAX_LINE - 1) {
                memmove(&buf[start + mlen], &buf[*posp], *lenp - *posp);
                memcpy(&buf[start], match, mlen);
                *lenp += mlen - (*posp - start);
                *posp = start + mlen;
                buf[*lenp] = '\0';
                redraw_line(prompt, buf, *disp_lenp, *posp);
                if (*lenp > *disp_lenp)
                    *disp_lenp = *lenp;
            }
        } else if (mcount > 1) {
            qsort(matches, mcount, sizeof(char *), cmpstr);
            printf("\r\n");
            for (int i = 0; i < mcount; i++)
                printf("%s%s", matches[i], i == mcount - 1 ? "" : " ");
            printf("\r\n");
            redraw_line(prompt, buf, *disp_lenp, *posp);
            if (*lenp > *disp_lenp)
                *disp_lenp = *lenp;
        }
        for (int i = 0; i < mcount; i++)
            free(matches[i]);
        free(matches);
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
            handle_backspace(buf, &len, &pos);
        } else if (c == 0x01) { /* Ctrl-A */
            while (pos > 0) {
                printf("\b");
                pos--;
            }
            fflush(stdout);
        } else if (c == 0x05) { /* Ctrl-E */
            while (pos < len) {
                printf("\x1b[C");
                pos++;
            }
            fflush(stdout);
        } else if (c == 0x15) { /* Ctrl-U */
            handle_line_start_erase(prompt, buf, &len, &pos, &disp_len);
        } else if (c == 0x17) { /* Ctrl-W */
            handle_word_erase(prompt, buf, &len, &pos, &disp_len);
        } else if (c == 0x0b) { /* Ctrl-K */
            handle_line_end_erase(prompt, buf, &len, &pos, &disp_len);
        } else if (c == 0x0c) { /* Ctrl-L */
            printf("\x1b[H\x1b[2J");
            redraw_line(prompt, buf, disp_len, pos);
            fflush(stdout);
        } else if (c == '\t') { /* Tab completion */
            handle_completion(prompt, buf, &len, &pos, &disp_len);
        } else if (c == 0x12) { /* Ctrl-R */
            int r = reverse_search(prompt, buf, &len, &pos, &disp_len);
            if (r < 0) {
                len = -1;
                break;
            } else if (r > 0) {
                break;
            }
        } else if (c == 0x13) { /* Ctrl-S */
            int r = forward_search(prompt, buf, &len, &pos, &disp_len);
            if (r < 0) {
                len = -1;
                break;
            } else if (r > 0) {
                break;
            }
        } else if (c == '\033') { /* escape sequences */
            char seq[3];
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
                        redraw_line(prompt, buf, disp_len, pos);
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
                    redraw_line(prompt, buf, disp_len, pos);
                    disp_len = len;
                } else if (seq[1] >= '0' && seq[1] <= '9') {
                    if (read(STDIN_FILENO, &seq[2], 1) != 1)
                        continue;
                    if (seq[1] == '1' && seq[2] == '~') { /* Home */
                        while (pos > 0) {
                            printf("\b");
                            pos--;
                        }
                        fflush(stdout);
                    } else if (seq[1] == '4' && seq[2] == '~') { /* End */
                        while (pos < len) {
                            printf("\x1b[C");
                            pos++;
                        }
                        fflush(stdout);
                    }
                }
            }
        } else if (c >= 32 && c < 127) { /* printable */
            handle_insert(buf, &len, &pos, c, &disp_len);
        }
    }

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig);

    if (len < 0)
        return NULL;
    buf[len] = '\0';
    return strdup(buf);
}

