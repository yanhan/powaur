#include <stdlib.h>
#include <stdio.h>

#include "hash.h"
#include "wrapper.h"

/* Adapted from pacman */
static const size_t prime_list[] =
{
	109ul, 227ul, 467ul, 953ul, 2029ul, 4349ul, 4703ul,
	10273ul, 22447ul, 45481ul, 92203ul, 202409ul, 410857ul,
	834181ul, 902483ul, 976369ul, 20000003ul
};

static int prime_list_sz = sizeof(prime_list) / sizeof(prime_list[0]);

/* For use in bsearch function */
static int size_t_cmp(const void *a, const void *b)
{
	return *(const size_t *) a - *(const size_t *) b;
}

/* Returns pointer to data in hash table if it exists */
static struct hash_table_entry *hash_lookup(unsigned long hash,
											struct hash_table *htable, void *data)
{
	unsigned int pos = hash % htable->sz;
	struct hash_table_entry *array = htable->table;

	while (array[pos].data) {
		if (array[pos].hash == hash) {
			if (!htable->cmp(array[pos].data, data)) {
				return array + pos;
			}
		}

		if (++pos >= htable->sz) {
			pos = 0;
		}
	}

	return NULL;
}

static void hash_entry_insert(unsigned long hash, struct hash_table *htable, void *data)
{
	unsigned int pos = hash % htable->sz;
	struct hash_table_entry *array = htable->table;

	while (array[pos].data) {
		if (array[pos].hash == hash) {
			if (!htable->cmp(array[pos].data, data)) {
				return;
			}
		}

		if (++pos >= htable->sz) {
			pos = 0;
		}
	}

	array[pos].hash = hash;
	array[pos].data = data;
	htable->nr++;
}

/* Grows a hash table based on prime_list
 * returns 0 on success, -1 on failure.
 */
static int hash_grow(struct hash_table *htable)
{
	unsigned int new_size;
	const size_t *ptr;

	ptr = bsearch(&htable->sz, prime_list, prime_list_sz, sizeof(size_t), size_t_cmp);

	if (!ptr || ptr - prime_list + 1 >= prime_list_sz) {
		/* Exceed size, +1 for now
		 * TODO: Find better allocation scheme
		 */
		new_size = htable->sz + 1;
	} else {
		new_size = ptr[1];
	}

	if (new_size <= htable->sz) {
		return -1;
	}

	/* Rehash */
	struct hash_table_entry *new_table = xcalloc(new_size, sizeof(struct hash_table_entry));
	struct hash_table_entry *old_table = htable->table;
	int i;
	unsigned int old_sz = htable->sz;
	unsigned long hash;

	htable->table = new_table;
	htable->sz = new_size;
	htable->nr = 0;
	for (i = 0; i < old_sz; ++i) {
		if (old_table[i].data) {
			hash = htable->hash(old_table[i].data);
			hash_entry_insert(hash, htable, old_table[i].data);
		}
	}

	free(old_table);
	return 0;
}

struct hash_table *hash_new(unsigned long (*hashfn) (void *),
		int (*hashcmp) (const void *, const void *))
{
	struct hash_table *htable = xcalloc(1, sizeof(struct hash_table));
	htable->sz = prime_list[0];
	htable->table = xcalloc(htable->sz, sizeof(struct hash_table_entry));
	htable->nr = 0;
	htable->hash = hashfn;
	htable->cmp = hashcmp;

	return htable;
}

void hash_free(struct hash_table *htable)
{
	if (!htable) {
		return;
	}

	free(htable->table);
	free(htable);
}

void hash_insert(struct hash_table *htable, void *data)
{
	unsigned long hash;
	unsigned int pos;
	void *ptr;

	hash = htable->hash(data);
	ptr = hash_lookup(hash, htable, data);
	if (ptr) {
		return;
	}

	/* Maintain load factor of 1/2 */
	if (htable->nr >= htable->sz / 2) {
		if (hash_grow(htable)) {
			return;
		}
	}

	hash_entry_insert(hash, htable, data);
}

void *hash_search(struct hash_table *htable, void *data)
{
	struct hash_table_entry *entry;
	unsigned long hash = htable->hash(data);
	
	entry = hash_lookup(hash, htable, data);
	return entry? entry->data : NULL;
}

int hash_pos(struct hash_table *htable, void *data)
{
	struct hash_table_entry *entry;
	unsigned long hash = htable->hash(data);
	entry = hash_lookup(hash, htable, data);

	if (entry) {
		return entry - htable->table;
	}

	return -1;
}
