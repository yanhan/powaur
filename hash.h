#ifndef POWAUR_HASH_H
#define POWAUR_HASH_H

/* Opaque */
struct hash_table;

/* hash table type */
enum hash_type {
	HASH_TABLE,
	VINDEX,
	HASH_BST
};

struct vidx_node {
	void *data;
	int idx;
};

typedef unsigned long (*pw_hash_fn) (void *);
typedef int (*pw_hashcmp_fn) (const void *, const void *);

struct hash_table *hash_new(enum hash_type type, pw_hash_fn hash,
							pw_hashcmp_fn hashcmp);
void hash_free(struct hash_table *htable);
void hash_insert(struct hash_table *table, void *data);
void *hash_search(struct hash_table *table, void *data);

/* Given a piece of data, find its position in the table.
 * returns index of data if found, returns -1 if not found */
int hash_pos(struct hash_table *table, void *data);

/* Walks through the hash table */
void hash_walk(struct hash_table *table, void (*walk) (void *data));

/* returns an appropriate allocation size */
unsigned int new_alloc_size(unsigned int old_sz);


/* Associative array, maps key to a tree of values */
struct hashmap;

/* tree_search function prototype
 * @param search external search structure
 * @param val value to search for
 */
typedef void *(*hmap_tree_search_fn) (void *search, void *val);

struct hashmap *hashmap_new(pw_hash_fn hashfn, pw_hashcmp_fn hashcmp);
void hashmap_free(struct hashmap *hmap);
void hashmap_insert(struct hashmap *hmap, void *key, void *val);

/* Search for a key in search structure search fulfilling criteria given by fn
 * @param hmap hash map
 * @param key key to search for in hash map
 * @param search external search structure
 * @param fn function to use in search walking
 */
void *hashmap_tree_search(struct hashmap *hmap, void *key, void *search, hmap_tree_search_fn fn);

void hashmap_walk(struct hashmap *hmap, void (*walk) (void *key, void *val));

#endif
