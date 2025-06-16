#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <errno.h>
static int count = 0;
static int fail_at = 0;
static char *(*real_strdup)(const char *);
static void __attribute__((constructor)) init(void){const char *p=getenv("FAIL_STRDUP_AT"); if(p) fail_at=atoi(p); real_strdup=dlsym(RTLD_NEXT,"strdup");}
char *strdup(const char *s){count++; if(fail_at>0 && count==fail_at){errno=ENOMEM; return NULL;} return real_strdup(s);}
