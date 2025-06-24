/*
 * vush - a simple UNIX shell
 * Licensed under the BSD 2-Clause Simplified License.
 * Mail checking helpers.
 */

#ifndef MAIL_H
#define MAIL_H
#include <time.h>
void free_mail_list(void);
void check_mail(void);
/* helper functions exported for completeness */
struct MailEntry;
struct MailEntry *find_mail_entry(const char *path);
void remember_mail_time(const char *path, time_t mtime);
#endif /* MAIL_H */
