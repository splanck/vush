#ifndef HASH_H
#define HASH_H

/* Command lookup caching. */

/* Return cached path for NAME or NULL if not cached.
 * When FD is non-NULL the associated file descriptor is stored in *FD. */
const char *hash_lookup(const char *name, int *fd);

/* Add NAME to the cache by searching PATH.  Returns 0 on success. */
int hash_add(const char *name);

/* Manually add NAME with PATH to the cache. Returns 0 on success. */
int hash_add_path(const char *name, const char *path);

/* Remove cached entry for NAME if it exists. */
void hash_remove(const char *name);

/* Remove all cached entries. */
void hash_clear(void);

/* Print all cached entries to stdout. */
void hash_print(void);

#endif /* HASH_H */
