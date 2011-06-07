#ifndef POWAUR_HASHDB_H
#define POWAUR_HASHDB_H

#include "hash.h"
#include "memlist.h"

/* Used for dependency resolution */
struct pw_hashdb {
	/* Tables of struct pkgpair */
	struct hash_table *local;
	struct hash_table *sync;
	struct hash_table *aur;

	struct hashbst *local_provides;
	struct hashbst *sync_provides;

	/* Cache provided->providing key-value mapping */
	struct hashmap *provides_cache;

	/* Backing store for strings and pkgpair */
	struct memlist *strpool;
	struct memlist *pkgpool;
};

struct pw_hashdb *hashdb_new(void);
void hashdb_free(struct pw_hashdb *hashdb);
struct pw_hashdb *build_hashdb(void);

/* Used for hashing, pkg can be pmpkg_t or aurpkg_t */
struct pkgpair {
	const char *pkgname;
	void *pkg;
};

unsigned long pkgpair_sdbm(void *pkg);
int pkgpair_cmp(const void *a, const void *b);
/* Searches htable for given package val
 * Provided to hashbst_tree_search */
void *provides_search(void *htable, void *val);

#endif
