#include <stdlib.h>

#include "hash.h"
#include "wrapper.h"

/* Internal use */
struct hash_table_entry {
	unsigned long hash;
	void *data;
};

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

/* Returns pointer to data in hash table if it exists */
static struct hash_table_entry *hash_lookup(unsigned long hash,
											struct hash_table *htable, void *data)
{
	unsigned int pos = hash % htable->sz;
	struct hash_table_entry *array = htable->table;

	while (array[pos].data) {
		if (array[pos].hash == hash) {
			if (!htable->cmp(array[pos].data, data)) {
				break;
			}
		}

		if (++pos >= htable->sz) {
			pos = 0;
		}
	}

	return array + pos;
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
	new_size = new_alloc_size(htable->sz);

	if (new_size <= htable->sz) {
		return -1;
	}

	/* Rehash */
	struct hash_table_entry *new_table = xcalloc(new_size, sizeof(struct hash_table_entry));
	struct hash_table_entry *old_table = htable->table;
	unsigned int i;
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
	/* Maintain load factor of 1/2 */
	if (htable->nr >= htable->sz / 2) {
		if (hash_grow(htable)) {
			return;
		}
	}

	unsigned long hash = htable->hash(data);
	struct hash_table_entry *entry = hash_lookup(hash, htable, data);
	if (!entry->data) {
		entry->hash = hash;
		entry->data = data;
		htable->nr++;
	}
}

void *hash_search(struct hash_table *htable, void *data)
{
	struct hash_table_entry *entry;
	unsigned long hash = htable->hash(data);
	
	entry = hash_lookup(hash, htable, data);
	return entry->data ? entry->data : NULL;
}

int hash_pos(struct hash_table *htable, void *data)
{
	struct hash_table_entry *entry;
	unsigned long hash = htable->hash(data);
	entry = hash_lookup(hash, htable, data);

	if (entry->data) {
		return entry - htable->table;
	}

	return -1;
}
