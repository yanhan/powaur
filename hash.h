#ifndef POWAUR_HASH_H
#define POWAUR_HASH_H

/* Opaque */
struct hash_table;

typedef unsigned long (*pw_hash_fn) (void *);
typedef int (*pw_hashcmp_fn) (const void *, const void *);

struct hash_table *hash_new(unsigned long (*hashfn) (void *),
		int (*hashcmp) (const void *, const void *));
void hash_free(struct hash_table *htable);
void hash_insert(struct hash_table *table, void *data);
void *hash_search(struct hash_table *table, void *data);

/* Given a piece of data, find its position in the table.
 * returns index of data if found, returns -1 if not found */
int hash_pos(struct hash_table *table, void *data);

/* returns an appropriate allocation size */
unsigned int new_alloc_size(unsigned int old_sz);

#endif
