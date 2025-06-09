#ifndef HISTORY_SEARCH_H
#define HISTORY_SEARCH_H

int reverse_search(const char *prompt, char *buf, int *lenp, int *posp,
                   int *disp_lenp);
int forward_search(const char *prompt, char *buf, int *lenp, int *posp,
                   int *disp_lenp);
int handle_history_search(char c, const char *prompt, char *buf,
                          int *lenp, int *posp, int *disp_lenp);

#endif /* HISTORY_SEARCH_H */
