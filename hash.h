#ifndef POWAUR_HASH_H
#define POWAUR_HASH_H

/* Generic hash table, linear probing.
 * Slightly modified from Git
 */

struct hash_table_entry {
	unsigned long hash;
	void *data;
};

struct hash_table {
	struct hash_table_entry *table;
	unsigned long (*hash) (void *);
	int (*cmp) (const void *, const void *);
	unsigned int sz;
	unsigned int nr;
};

struct hash_table *hash_new(unsigned long (*hashfn) (void *),
		int (*hashcmp) (const void *, const void *));
void hash_free(struct hash_table *htable);
void hash_insert(struct hash_table *table, void *data);
void *hash_search(struct hash_table *table, void *data);

#endif
