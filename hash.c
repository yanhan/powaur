#include <stdlib.h>

#include "hash.h"
#include "wrapper.h"

/* Adapted from pacman */
static const unsigned int prime_list[] =
{
	109, 227, 467, 953, 2029, 4349, 4703,
	10273, 22447, 45481, 92203, 202409, 410857,
	834181, 902483, 976369, 20000003
};

static int prime_list_sz = sizeof(prime_list) / sizeof(prime_list[0]);

/* For use in bsearch function */
static int uint_cmp(const void *a, const void *b)
{
	return *(const unsigned int *) a - *(const unsigned int *) b;
}

unsigned int new_alloc_size(unsigned int old_size)
{
	unsigned int *ptr;
	unsigned int new_size;
	ptr = bsearch(&old_size, prime_list, prime_list_sz, sizeof(unsigned int ), uint_cmp);

	if (!ptr || ptr - prime_list + 1 >= prime_list_sz) {
		new_size = (old_size + 16) * 3/2;
	} else {
		new_size = ptr[1];
	}

	return new_size;
}

/*******************************************************************************
 *
 * Internal use only
 *
 ******************************************************************************/

typedef void (*hash_free_fn) (struct hash_table *htable);
typedef void (*hash_insert_fn) (struct hash_table *htable, void *data);
typedef void * (*hash_search_fn) (struct hash_table *htable, void *data);
typedef int (*hash_pos_fn) (struct hash_table *htable, void *data);

struct hash_vtbl {
	hash_free_fn free;
	hash_insert_fn insert;
	hash_search_fn search;
	hash_pos_fn pos;
};

struct hash_table_entry {
	unsigned long hash;

	union {
		/* Normal hash table */
		void *data;

		/* vindex */
		struct vidx_node vidx;
	} u;
};

struct hash_table {
	struct hash_table_entry *table;
	unsigned long (*hash) (void *);
	int (*cmp) (const void *, const void *);
	unsigned int sz;
	unsigned int nr;

	struct hash_vtbl *vtbl;
	enum hash_type type;
};

/*******************************************************************************
 *
 * Public functions
 ******************************************************************************/

/* Forward declaration */
static void hash_setup_htable(struct hash_table *htable);
static void hash_setup_vindex(struct hash_table *htable);

struct hash_table *hash_new(enum hash_type type, pw_hash_fn hashfn,
							pw_hashcmp_fn hashcmp)
{
	struct hash_table *htable = xcalloc(1, sizeof(struct hash_table));
	htable->sz = prime_list[0];
	htable->table = xcalloc(htable->sz, sizeof(struct hash_table_entry));
	htable->nr = 0;
	htable->hash = hashfn;
	htable->cmp = hashcmp;
	htable->type = type;

	switch (type) {
	case VINDEX:
		hash_setup_vindex(htable);
		break;
	default:
		/* Default to HASH_TABLE */
		hash_setup_htable(htable);
		break;
	}

	return htable;
}

void hash_free(struct hash_table *htable)
{
	htable->vtbl->free(htable);
}

void hash_insert(struct hash_table *htable, void *data)
{
	htable->vtbl->insert(htable, data);
}

void *hash_search(struct hash_table *htable, void *data)
{
	return htable->vtbl->search(htable, data);
}

int hash_pos(struct hash_table *htable, void *data)
{
	return htable->vtbl->pos(htable, data);
}

/*******************************************************************************
 *
 * Normal hash table
 *
 *****************************************************************************/

/* Forward declaration */
static void hash_free_htable(struct hash_table *htable);
static void hash_insert_htable(struct hash_table *htable, void *data);
static void *hash_search_htable(struct hash_table *htable, void *data);
static int hash_pos_htable(struct hash_table *htable, void *data);

static struct hash_vtbl hash_vtbl_htable = {
	hash_free_htable,
	hash_insert_htable,
	hash_search_htable,
	hash_pos_htable
};

static void hash_setup_htable(struct hash_table *htable)
{
	htable->vtbl = &hash_vtbl_htable;
}

static void hash_entry_insert_htable(unsigned long hash, struct hash_table *htable, void *data)
{
	unsigned int pos = hash % htable->sz;
	struct hash_table_entry *array = htable->table;

	while (array[pos].u.data) {
		if (array[pos].hash == hash) {
			if (!htable->cmp(array[pos].u.data, data)) {
				return;
			}
		}

		if (++pos >= htable->sz) {
			pos = 0;
		}
	}

	array[pos].hash = hash;
	array[pos].u.data = data;
	htable->nr++;
}

/* Returns pointer to data in hash table if it exists */
static struct hash_table_entry *hash_entry_lookup_htable(unsigned long hash,
											struct hash_table *htable, void *data)
{
	unsigned int pos = hash % htable->sz;
	struct hash_table_entry *array = htable->table;

	while (array[pos].u.data) {
		if (array[pos].hash == hash) {
			if (!htable->cmp(array[pos].u.data, data)) {
				break;
			}
		}

		if (++pos >= htable->sz) {
			pos = 0;
		}
	}

	return array + pos;
}

/* Grows a hash table based on prime_list
 * returns 0 on success, -1 on failure.
 */
static int hash_grow_htable(struct hash_table *htable)
{
	unsigned int new_size;
	new_size = new_alloc_size(htable->sz);

	if (new_size <= htable->sz) {
		return -1;
	}

	/* Rehash */
	struct hash_table_entry *old_table = htable->table;
	unsigned int i;
	unsigned int old_sz = htable->sz;
	unsigned long hash;

	htable->table = xcalloc(new_size, sizeof(struct hash_table_entry));
	htable->sz = new_size;
	htable->nr = 0;

	for (i = 0; i < old_sz; ++i) {
		if (old_table[i].u.data) {
			hash = htable->hash(old_table[i].u.data);
			hash_entry_insert_htable(hash, htable, old_table[i].u.data);
		}
	}

	free(old_table);
	return 0;
}

static void hash_free_htable(struct hash_table *htable)
{
	if (!htable) {
		return;
	}

	free(htable->table);
	free(htable);
}

void hash_insert_htable(struct hash_table *htable, void *data)
{
	/* Maintain load factor of 1/2 */
	if (htable->nr >= htable->sz / 2) {
		if (hash_grow_htable(htable)) {
			return;
		}
	}

	unsigned long hash = htable->hash(data);
	struct hash_table_entry *entry = hash_entry_lookup_htable(hash, htable, data);
	if (!entry->u.data) {
		entry->hash = hash;
		entry->u.data = data;
		htable->nr++;
	}
}

void *hash_search_htable(struct hash_table *htable, void *data)
{
	struct hash_table_entry *entry;
	unsigned long hash = htable->hash(data);
	
	entry = hash_entry_lookup_htable(hash, htable, data);
	return entry->u.data ? entry->u.data : NULL;
}

int hash_pos_htable(struct hash_table *htable, void *data)
{
	struct hash_table_entry *entry;
	unsigned long hash = htable->hash(data);
	entry = hash_entry_lookup_htable(hash, htable, data);

	if (entry->u.data) {
		return entry - htable->table;
	}

	return -1;
}

/******************************************************************************
 *
 * VINDEX
 *
 *****************************************************************************/

/* Forward declaration */
static void hash_insert_vindex(struct hash_table *htable, void *data);
static void *hash_search_vindex(struct hash_table *htable, void *data);
static int hash_pos_vindex(struct hash_table *htable, void *data);

static struct hash_vtbl hash_vtbl_vindex = {
	/* Share free function */
	hash_free_htable,
	hash_insert_vindex,
	hash_search_vindex,
	hash_pos_vindex
};

static void hash_setup_vindex(struct hash_table *htable)
{
	htable->vtbl = &hash_vtbl_vindex;
}

static void hash_entry_insert_vindex(unsigned long hash, struct hash_table *htable,
									 void *data, int idx)
{
	unsigned int pos = hash % htable->sz;
	struct hash_table_entry *table = htable->table;

	while (table[pos].u.vidx.data) {
		if (table[pos].hash == hash && !htable->cmp(table[pos].u.vidx.data, data)) {
			return;
		}

		if (++pos >= htable->sz) {
			pos = 0;
		}
	}

	table[pos].hash = hash;
	table[pos].u.vidx.data = data;
	table[pos].u.vidx.idx = idx;
	htable->nr++;
}

/* Returns pointer to data in hash table if it exists */
static struct hash_table_entry *hash_entry_lookup_vindex(unsigned long hash,
		struct hash_table *htable, void *data)
{
	unsigned int pos = hash % htable->sz;
	struct hash_table_entry *array = htable->table;

	while (array[pos].u.vidx.data) {
		if (array[pos].hash == hash) {
			if (!htable->cmp(array[pos].u.vidx.data, data)) {
				break;
			}
		}

		if (++pos >= htable->sz) {
			pos = 0;
		}
	}

	return array + pos;
}

/* Grows a hash table based on prime_list
 * returns 0 on success, -1 on failure.
 */
static int hash_grow_vindex(struct hash_table *htable)
{
	unsigned int new_size = new_alloc_size(htable->sz);
	if (new_size <= htable->sz) {
		return -1;
	}

	/* Rehash */
	struct hash_table_entry *old_table = htable->table;
	unsigned int i;
	unsigned int old_size = htable->sz;
	unsigned long hash;

	htable->table = xcalloc(new_size, sizeof(struct hash_table_entry));
	htable->sz = new_size;
	htable->nr = 0;

	for (i = 0; i < old_size; ++i) {
		if (old_table[i].u.vidx.data) {
			hash_entry_insert_vindex(old_table[i].hash, htable, old_table[i].u.data,
									 old_table[i].u.vidx.idx);
		}
	}

	free(old_table);
	return 0;
}

/* @param data a struct vidx_node * */
void hash_insert_vindex(struct hash_table *htable, void *data)
{
	/* Maintain load factor of 1/2 */
	if (htable->nr >= htable->sz / 2) {
		if (hash_grow_vindex(htable)) {
			return;
		}
	}

	struct vidx_node *node = data;
	unsigned long hash = htable->hash(node->data);
	struct hash_table_entry *entry = hash_entry_lookup_vindex(hash, htable, node->data);
	if (!entry->u.vidx.data) {
		entry->hash = hash;
		entry->u.vidx.data = node->data;
		entry->u.vidx.idx = node->idx;
		htable->nr++;
	}
}

void *hash_search_vindex(struct hash_table *htable, void *data)
{
	struct hash_table_entry *entry;
	unsigned long hash = htable->hash(data);

	entry = hash_entry_lookup_vindex(hash, htable, data);
	return entry->u.vidx.data ? entry->u.vidx.data : NULL;
}

/* This does NOT return where the data is in the hash table.
 * It returns where the data is in the adjacency list of the graph, which is
 * given by the idx field in entry->u.vidx
 */
int hash_pos_vindex(struct hash_table *htable, void *data)
{
	struct hash_table_entry *entry;
	unsigned long hash = htable->hash(data);
	entry = hash_entry_lookup_vindex(hash, htable, data);

	if (entry->u.vidx.data) {
		return entry->u.vidx.idx;
	}

	return -1;
}
