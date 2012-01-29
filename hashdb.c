#include <stdlib.h>
#include <string.h>
#include <alpm.h>
#include "environment.h"
#include "hashdb.h"
#include "hash.h"
#include "memlist.h"
#include "powaur.h"
#include "wrapper.h"
#include "util.h"

struct pw_hashdb *hashdb_new(void)
{
	struct pw_hashdb *hashdb = xcalloc(1, sizeof(struct pw_hashdb));

	/* Local, sync, AUR db hash tables of struct pkgpair */
	hashdb->local = hash_new(HASH_TABLE, pkgpair_sdbm, pkgpair_cmp);
	hashdb->sync  = hash_new(HASH_TABLE, pkgpair_sdbm, pkgpair_cmp);
	hashdb->aur   = hash_new(HASH_TABLE, pkgpair_sdbm, pkgpair_cmp);

	hashdb->aur_downloaded = hash_new(HASH_TABLE, (pw_hash_fn) sdbm, (pw_hashcmp_fn) strcmp);
	hashdb->aur_outdated   = hash_new(HASH_TABLE, (pw_hash_fn) sdbm, (pw_hashcmp_fn) strcmp);

	/* Local and sync provides */
	hashdb->local_provides = hashbst_new((pw_hash_fn) sdbm, (pw_hashcmp_fn) strcmp);
	hashdb->sync_provides  = hashbst_new((pw_hash_fn) sdbm, (pw_hashcmp_fn) strcmp);

	/* Cache provided->providing key-value mapping */
	hashdb->provides_cache = hashmap_new((pw_hash_fn) sdbm, (pw_hashcmp_fn) strcmp);
	hashdb->pkg_from = hashmap_new((pw_hash_fn) sdbm, (pw_hashcmp_fn) strcmp);

	hashdb->strpool = memlist_new(4096, sizeof(char *), MEMLIST_PTR);
	hashdb->pkgpool = memlist_new(4096, sizeof(struct pkgpair), MEMLIST_NORM);

	/* Initialize pkgfrom_t */
	hashdb->pkg_from_unknown = PKG_FROM_UNKNOWN;
	hashdb->pkg_from_local   = PKG_FROM_LOCAL;
	hashdb->pkg_from_sync    = PKG_FROM_SYNC;
	hashdb->pkg_from_aur     = PKG_FROM_AUR;
	return hashdb;
}

void hashdb_free(struct pw_hashdb *hashdb)
{
	if (!hashdb) {
		return;
	}

	hash_free(hashdb->local);
	hash_free(hashdb->sync);
	hash_free(hashdb->aur);
	hash_free(hashdb->aur_downloaded);
	hash_free(hashdb->aur_outdated);
	alpm_list_free(hashdb->immediate_deps);
	hashbst_free(hashdb->local_provides);
	hashbst_free(hashdb->sync_provides);
	hashmap_free(hashdb->provides_cache);
	hashmap_free(hashdb->pkg_from);
	memlist_free(hashdb->strpool);
	memlist_free(hashdb->pkgpool);
	free(hashdb);
}

/* hashes packages and their provides
 * @param dbcache list of alpm_pkg_t * to be hashed
 * @param htable hash table hashing struct pkgpair
 * @param provides hashbst used to hash provides
 * @param hashdb hash database
 */
static void hash_packages(alpm_list_t *dbcache, struct hash_table *htable,
						  struct hashbst *provides, struct pw_hashdb *hashdb)
{
	alpm_list_t *i, *k;
	alpm_pkg_t *pkg;
	alpm_depend_t *dep;
	struct pkgpair pkgpair;
	void *memlist_ptr;

	char buf[1024];
	char *dupstr;
	const char *pkgname;

	for (i = dbcache; i; i = i->next) {
		pkg = i->data;
		pkgname = alpm_pkg_get_name(pkg);

		pkgpair.pkgname = pkgname;
		pkgpair.pkg = pkg;
		memlist_ptr = memlist_add(hashdb->pkgpool, &pkgpair);
		hash_insert(htable, memlist_ptr);

		/* Provides */
		for (k = alpm_pkg_get_provides(pkg); k; k = k->next) {
			dep = k->data;
			snprintf(buf, 1024, "%s", dep->name);
			if (!strtrim_ver(buf)) {
				continue;
			}

			dupstr = xstrdup(buf);
			memlist_ptr = memlist_add(hashdb->strpool, &dupstr);
			hashbst_insert(provides, memlist_ptr, (void *) pkgname);
		}
	}
}

struct pw_hashdb *build_hashdb(void)
{
	alpm_list_t *i, *k, *syncdbs, *dbcache;
	alpm_db_t *db;
	alpm_pkg_t *pkg;

	char buf[1024];
	const char *pkgname;
	char *dupstr;
	struct pkgpair pkgpair;
	void *memlist_ptr;

	struct pw_hashdb *hashdb = hashdb_new();

	db = alpm_option_get_localdb(config->handle);
	if (!db) {
		error(PW_ERR_LOCALDB_NULL);
		goto error_cleanup;
	}

	dbcache = alpm_db_get_pkgcache(db);
	if (!dbcache) {
		error(PW_ERR_LOCALDB_CACHE_NULL);
		goto error_cleanup;
	}

	hash_packages(dbcache, hashdb->local, hashdb->local_provides, hashdb);

	syncdbs = alpm_option_get_syncdbs(config->handle);
	for (i = syncdbs; i; i = i->next) {
		db = i->data;
		hash_packages(alpm_db_get_pkgcache(db), hashdb->sync, hashdb->sync_provides,
					  hashdb);
	}

	/* Compute AUR packages */
	for (i = dbcache; i; i = i->next) {
		pkg = i->data;
		pkgpair.pkgname = alpm_pkg_get_name(pkg);
		pkgpair.pkg = pkg;
		if (!hash_search(hashdb->sync, &pkgpair)) {
			memlist_ptr = memlist_add(hashdb->pkgpool, &pkgpair);
			hash_insert(hashdb->aur, memlist_ptr);
			hashmap_insert(hashdb->pkg_from, (void *) pkgpair.pkgname, &hashdb->pkg_from_aur);
		}
	}

	return hashdb;

error_cleanup:
	hashdb_free(hashdb);
	return NULL;
}

unsigned long pkgpair_sdbm(void *pkg)
{
	const struct pkgpair *pair = pkg;
	return sdbm(pair->pkgname);
}

int pkgpair_cmp(const void *a, const void *b)
{
	/* Some struct pkgpair have no backing pkg yet */
	if (!a || !b) {
		return 1;
	}

	const struct pkgpair *pair1 = a;
	const struct pkgpair *pair2 = b;
	return strcmp(pair1->pkgname, pair2->pkgname);
}

void *provides_search(void *htable, void *val)
{
	struct hash_table *hash = htable;
	struct pkgpair pkgpair = {
		val, NULL
	};
	return hash_search(hash, &pkgpair);
}
