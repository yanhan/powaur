#ifndef POWAUR_HASH_H
#define POWAUR_HASH_H

#include <alpm.h>

/* Opaque */
struct hash_table;

/* hash table type */
enum hash_type {
	HASH_TABLE,
	VINDEX,
	HASH_BST,
	HASH_MAP
};

/* Exposed for graph data structure */
struct vidx_node {
	void *data;
	int idx;
};

/* returns an appropriate allocation size */
unsigned int new_alloc_size(unsigned int old_sz);

/*******************************************************************************
 *
 * General hash table functions
 *
 ******************************************************************************/

typedef unsigned long (*pw_hash_fn) (void *);
typedef int (*pw_hashcmp_fn) (const void *, const void *);

struct hash_table *hash_new(enum hash_type type, pw_hash_fn hash, pw_hashcmp_fn hashcmp);
void hash_free(struct hash_table *htable);
void hash_insert(struct hash_table *table, void *data);
void *hash_search(struct hash_table *table, void *data);

/* Given a piece of data, find its position in the table.
 * returns index of data if found, returns -1 if not found */
int hash_pos(struct hash_table *table, void *data);

/* Returns a list of data pointers in the hash table.
 * This is only valid for HASH_TABLE for now. It will return NULL for other tables
 */
alpm_list_t *hash_to_list(struct hash_table *table);

/*******************************************************************************
 *
 * Hash BST functions
 *
 ******************************************************************************/

/* Associative array, maps key to a tree of values */
struct hashbst;

/* tree_search function prototype
 * @param search external search structure
 * @param val value to search for
 */
typedef void *(*hbst_search_fn) (void *search, void *val);

struct hashbst *hashbst_new(pw_hash_fn hashfn, pw_hashcmp_fn hashcmp);
void hashbst_free(struct hashbst *hbst);
void hashbst_insert(struct hashbst *hbst, void *key, void *val);

/* Search for a key in search structure search fulfilling criteria given by fn
 * @param hbst hash bst
 * @param key key to search for in hash bst
 * @param search external search structure
 * @param fn function to use in search walking
 */
void *hashbst_tree_search(struct hashbst *hbst, void *key, void *search, hbst_search_fn fn);

/*******************************************************************************
 *
 * Hash Map functions
 *
 ******************************************************************************/

/* Opaque */
struct hashmap;

struct hashmap *hashmap_new(pw_hash_fn hashfn, pw_hashcmp_fn hashcmp);
void hashmap_free(struct hashmap *hmap);
void hashmap_insert(struct hashmap *hmap, void *key, void *val);

/* Searches hashmap for a given key
 * returns the value corresponding to key if it's in hashmap, NULL otherwise.
 * @param hmap hash map
 * @param key key to search for
 */
void *hashmap_search(struct hashmap *hmap, void *key);

#endif
