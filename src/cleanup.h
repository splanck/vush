#ifndef VUSH_CLEANUP_H
#define VUSH_CLEANUP_H

#include <stdlib.h>
#include "strarray.h"

static inline void cleanup_freep(void *p) {
    free(*(void **)p);
}

static inline void cleanup_strarrayp(void *p) {
    strarray_release((StrArray *)p);
}

#define CLEANUP_FREE __attribute__((cleanup(cleanup_freep)))
#define CLEANUP_STRARRAY __attribute__((cleanup(cleanup_strarrayp)))

#endif /* VUSH_CLEANUP_H */
