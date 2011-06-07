#include <alpm.h>

#include "conf.h"
#include "curl.h"
#include "environment.h"
#include "graph.h"
#include "hash.h"
#include "memlist.h"
#include "package.h"
#include "powaur.h"
#include "query.h"
#include "util.h"

/* -Qi */
static int query_info(pmdb_t *localdb, alpm_list_t *targets)
{
	int ret, hits, found, pkgcount;
	alpm_list_t *i, *k, *dbcache;
	pmpkg_t *pkg;

	ret = pkgcount = hits = found = 0;
	dbcache = alpm_db_get_pkgcache(localdb);

	for (i = targets; i; i = i->next, ++pkgcount) {
		found = 0;
		for (k = dbcache; k; k = k->next) {
			pkg = k->data;
			if (!strcmp(i->data, alpm_pkg_get_name(pkg))) {
				if (hits++) {
					printf("\n");
				}

				found = 1;
				pacman_pkgdump(pkg, PKG_FROM_LOCAL);
				break;
			}
		}

		if (!found) {
			if (pkgcount) {
				printf("\n");
			}

			ret = -1;
			pw_fprintf(PW_LOG_ERROR, stderr, "package %s not found\n", i->data);
		}
	}

	return ret;
}

/* -Qs, only 1 target for now */
static int query_search(pmdb_t *localdb, const char *pkgname)
{
	int ret, found;
	const char *repo;
	alpm_list_t *i, *k, *dbcache, *groups;
	alpm_list_t *syncdbs;
	pmpkg_t *pkg;

	dbcache = alpm_db_get_pkgcache(localdb);
	syncdbs = alpm_option_get_syncdbs();

	for (k = dbcache; k; k = k->next) {
		pkg = k->data;
		groups = NULL;

		if (!strcmp(pkgname, alpm_pkg_get_name(pkg))) {
			repo = which_db(syncdbs, pkgname, &groups);
			color_repo(repo);
			printf("%s%s %s%s", color.bold, pkgname,
				   color.bgreen, alpm_pkg_get_version(pkg));
			color_groups(groups);
			printf("%s %s\n", TAB, alpm_pkg_get_desc(pkg));
			found = 1;
		}
	}

	return found ? 0: -1;
}

int powaur_query(alpm_list_t *targets)
{
	pmdb_t *localdb = alpm_option_get_localdb();
	if (!localdb) {
		return error(PW_ERR_INIT_LOCALDB);
	}

	alpm_list_t *dblist = NULL;
	alpm_list_t *i, *j, *dbcache;
	pmpkg_t *pkg, *spkg;
	int ret = 0, found;

	/* -i and -s conflicting options */
	if (config->op_q_info && config->op_q_search) {
		pw_fprintf(PW_LOG_ERROR, stderr, "-i (info) and -s (search) are "
				   "mutually exclusive.\n");
		return -1;
	}

	/* No targets */
	if (targets == NULL) {
		dblist = alpm_list_add(dblist, localdb);

		if (config->op_q_info) {
			/* -Qi, detailed info */
			ret = pacman_db_dump(PKG_FROM_LOCAL, DUMP_Q_INFO);
		} else if (config->op_q_search) {
			/* -Qs
			 * repo/pkg ver (grp)
			 * desc
			 */
			ret = pacman_db_dump(PKG_FROM_LOCAL, DUMP_Q_SEARCH);
		} else {
			/* -Q
			 * repo/pkg ver (grp)
			 */
			ret = pacman_db_dump(PKG_FROM_LOCAL, DUMP_Q);
		}

		alpm_list_free(dblist);
		return ret;
	}

	if (config->op_q_info) {
		ret = query_info(localdb, targets);
	} else if (config->op_q_search) {
		ret = query_search(localdb, targets->data);
	} else {
		/* Plain -Q */
		alpm_list_t *sdbs = alpm_option_get_syncdbs();
		dbcache = alpm_db_get_pkgcache(localdb);

		for (i = targets; i; i = i->next) {
			found = 0;
			for (j = dbcache; j; j = j->next) {
				pkg = j->data;
				if (!strcmp(i->data, alpm_pkg_get_name(pkg))) {
					print_pkg_pretty(sdbs, pkg, DUMP_Q);
					found = 1;
					break;
				}
			}

			if (!found) {
				printf("package \"%s\" not found\n", i->data);
				ret = -1;
			}
		}
	}

	return ret;
}

/* Used for hashing, pkg can be pmpkg_t or aurpkg_t */
struct pkgpair {
	const char *pkgname;
	void *pkg;
};

static unsigned long pkg_sdbm(void *pkg)
{
	const struct pkgpair *pair = pkg;
	return sdbm(pair->pkgname);
}

static int pkg_cmp(const void *a, const void *b)
{
	/* Some struct pkgpair have no backing pkg yet */
	if (!a || !b) {
		return 1;
	}

	const struct pkgpair *pair1 = a;
	const struct pkgpair *pair2 = b;
	return strcmp(pair1->pkgname, pair2->pkgname);
}

static void *provides_search(void *htable, void *val)
{
	struct hash_table *hash = htable;
	struct pkgpair pkgpair = {
		val, NULL
	};
	return hash_search(hash, &pkgpair);
}

/* Used for dependency resolution */
struct pw_hashdb {
	/* Tables of struct pkgpair */
	struct hash_table *local;
	struct hash_table *sync;

	struct hashbst *local_provides;
	struct hashbst *sync_provides;

	/* Cache provided->providing key-value mapping */
	struct hashmap *provides_cache;

	/* Backing store for strings and pkgpair */
	struct memlist *strpool;
	struct memlist *pkgpool;
};

static struct pw_hashdb *hashdb_new(void)
{
	struct pw_hashdb *hashdb = xcalloc(1, sizeof(struct pw_hashdb));

	/* Local and sync db hash tables of struct pkgpair */
	hashdb->local = hash_new(HASH_TABLE, pkg_sdbm, pkg_cmp);
	hashdb->sync = hash_new(HASH_TABLE, pkg_sdbm, pkg_cmp);

	/* Local and sync provides */
	hashdb->local_provides = hashbst_new((pw_hash_fn) sdbm, (pw_hashcmp_fn) strcmp);
	hashdb->sync_provides = hashbst_new((pw_hash_fn) sdbm, (pw_hashcmp_fn) strcmp);

	/* Cache provided->providing key-value mapping */
	hashdb->provides_cache = hashmap_new((pw_hash_fn) sdbm, (pw_hashcmp_fn) strcmp);

	hashdb->strpool = memlist_new(4096, sizeof(char *), MEMLIST_PTR);
	hashdb->pkgpool = memlist_new(4096, sizeof(struct pkgpair), MEMLIST_NORM);
	return hashdb;
}

static void hashdb_free(struct pw_hashdb *hashdb)
{
	hash_free(hashdb->local);
	hash_free(hashdb->sync);
	hashbst_free(hashdb->local_provides);
	hashbst_free(hashdb->sync_provides);
	hashmap_free(hashdb->provides_cache);
	memlist_free(hashdb->strpool);
	memlist_free(hashdb->pkgpool);
	free(hashdb);
}

/* Change provided package to a package which provides it.
 * @param hashdb hash database
 * @param pkg package name
 *
 * returns a package providing pkg if present, else returns pkg
 */
static const char *normalize_package(struct pw_hashdb *hashdb, const char *pkgname)
{
	const char *provided = NULL;
	struct pkgpair pkgpair;
	struct pkgpair *pkgptr;

	pkgpair.pkgname = pkgname;
	pkgpair.pkg = NULL;

	/* If it's in local / sync db, done */
	if (hash_search(hashdb->local, &pkgpair) || hash_search(hashdb->sync, &pkgpair)) {
		return pkgname;
	}

	/* Search cache */
	provided = hashmap_search(hashdb->provides_cache, (void *) pkgname);
	if (provided) {
		return provided;
	}

	/* Search local and sync provides */
	/* Search local provides tree */
	pkgptr = hashbst_tree_search(hashdb->local_provides, (void *) pkgname,
								 hashdb->local, provides_search);
	if (!pkgptr) {
		pkgptr = hashbst_tree_search(hashdb->local_provides, (void *) pkgname,
									 hashdb->sync, provides_search);
	}

	if (pkgptr) {
		/* Cache and return */
		hashmap_insert(hashdb->provides_cache, (void *) pkgname,
					   (void *) pkgptr->pkgname);
		provided = pkgptr->pkgname;
		goto done;
	}

	/* Search sync provides tree */
	pkgptr = hashbst_tree_search(hashdb->sync_provides, (void *) pkgname,
								 hashdb->local, provides_search);

	if (!pkgptr) {
		pkgptr = hashbst_tree_search(hashdb->sync_provides, (void *) pkgname,
									 hashdb->sync, provides_search);
	}

	if (pkgptr) {
		/* Cache and return */
		hashmap_insert(hashdb->provides_cache, (void *) pkgname, (void *) pkgptr->pkgname);
		provided = pkgptr->pkgname;
	}

done:
	return provided ? provided : pkgname;
}

/* @param hashdb hash database
 * @param curpkg current package we are resolving
 * @param dep_list pointer to list to store resulting dependencies
 * @param verbose switch on detailed dependency resolution
 *
 * returns 1 if curpkg is a "provided" and non-existent pkg, 0 otherwise.
 */
static int crawl_resolve(struct pw_hashdb *hashdb, struct pkgpair *curpkg,
						 alpm_list_t **dep_list, int verbose)
{
	alpm_list_t *i, *depmod_list, *deps = NULL;
	struct pkgpair *pkgpair;
	void *pkg_provides;
	const char *cache_result;
	const char *depname, *final_pkgname;

	/* Search in local and sync db first */
	pkgpair = hash_search(hashdb->local, curpkg);
	if (!pkgpair) {
		pkgpair = hash_search(hashdb->sync, curpkg);
	}

	/* If the pkg can be found in a db, just skip for non-verbose */
	if (!verbose && pkgpair) {
		dep_list = NULL;
		return 0;
	}

	if (!pkgpair) {
		goto search_provides;
	}

	/* pkg is installed, return its deps */
	depmod_list = alpm_pkg_get_depends(pkgpair->pkg);
	for (i = depmod_list; i; i = i->next) {
		depname = normalize_package(hashdb, alpm_dep_get_name(i->data));
		deps = alpm_list_add(deps, (void *) depname);
	}

	if (dep_list) {
		*dep_list = deps;
	} else {
		alpm_list_free(deps);
	}

	return 0;

search_provides:
	/* Search cache provides hash map */
	final_pkgname = normalize_package(hashdb, curpkg->pkgname);
	if (final_pkgname != curpkg->pkgname) {
		if (dep_list) {
			*dep_list = alpm_list_add(NULL, (void *) final_pkgname);
		}

		return 1;
	}

	return 0;
}

static void crawl_deps(struct pw_hashdb *hashdb, const char *pkgname)
{
	struct graph *graph = graph_new((pw_hash_fn) sdbm, (pw_hashcmp_fn) strcmp);
	struct stack *st = stack_new(sizeof(struct pkgpair));
	struct hash_table *resolved = hash_new(HASH_TABLE, (pw_hash_fn) sdbm,
										   (pw_hashcmp_fn) strcmp);

	int ret;
	struct pkgpair pkgpair, deppkg;
	const char *curpkg;
	alpm_list_t *i;
	alpm_list_t *deps;
	alpm_list_t *free_list = NULL;

	CURL *curl;
	curl = curl_easy_init();
	if (!curl) {
		error(PW_ERR_CURL_INIT);
		return;
	}

	pkgpair.pkgname = pkgname;
	pkgpair.pkg = NULL;

	stack_push(st, &pkgpair);
	while (!stack_empty(st)) {
		stack_pop(st, &pkgpair);
		deps = NULL;

		ret = crawl_resolve(hashdb, &pkgpair, &deps, 1);
		if (ret) {
			/* Provided package, just push onto stack */
			deppkg.pkgname = deps->data;
			deppkg.pkg = NULL;
			stack_push(st, &deppkg);
			alpm_list_free(deps);
			continue;
		} else if (hash_search(resolved, (void *) pkgpair.pkgname)) {
			goto cleanup_deps;
		}

		for (i = deps; i; i = i->next) {
			deppkg.pkgname = i->data;
			deppkg.pkg = NULL;
			stack_push(st, &deppkg);

			/* dep --> current */
			graph_add_edge(graph, i->data, (void *) pkgpair.pkgname);
		}

		hash_insert(resolved, (void *) pkgpair.pkgname);
cleanup_deps:
		alpm_list_free(deps);
	}

	struct stack *topost = stack_new(sizeof(int));
	int cycles = graph_toposort(graph, topost);
	if (cycles) {
		pw_printf(PW_LOG_INFO, "Unable to resolve package \"%s\" due to cyclic "
				  "dependencies.\n", pkgname);
		goto cleanup;
	}

	if (stack_empty(topost)) {
		printf("%s has no dependencies.\n", pkgname);
		goto cleanup;
	}

	int idx;
	int cnt = 0;

	printf("\n");
	pw_printf(PW_LOG_INFO, "\"%s\" topological order: ", pkgname);
	while (!stack_empty(topost)) {
		stack_pop(topost, &idx);
		curpkg = graph_get_vertex_data(graph, idx);
		if (!curpkg) {
			continue;
		}

		if (cnt++ > 0) {
			printf(" -> %s", curpkg);
		} else {
			printf("%s", curpkg);
		}
	}

	printf("\n");

cleanup:
	stack_free(st);
	stack_free(topost);
	hash_free(resolved);
	graph_free(graph);
	curl_easy_cleanup(curl);
}

/* TODO: Remove. For walking hash table */
void pkg_walk(void *pkg)
{
	struct pkgpair *pkgpair = pkg;
	printf("pkg = %s\n", pkgpair->pkgname);
}

/* TODO: Remove. For walking hash map */
void provides_walk(void *key, void *val)
{
	printf("%s is provided by %s\n", key, val);
}

/* hashes packages and their provides
 * @param dbcache list of pmpkg_t * to be hashed
 * @param htable hash table hashing struct pkgpair
 * @param provides hashbst used to hash provides
 */
static void hash_packages(alpm_list_t *dbcache, struct hash_table *htable,
						  struct hashbst *provides, struct pw_hashdb *hashdb)
{
	alpm_list_t *i, *k;
	pmpkg_t *pkg;
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
			snprintf(buf, 1024, "%s", k->data);
			if (!strtrim_ver(buf)) {
				continue;
			}

			dupstr = xstrdup(buf);
			memlist_ptr = memlist_add(hashdb->strpool, &dupstr);
			hashbst_insert(provides, memlist_ptr, (void *) pkgname);
		}
	}
}

int powaur_crawl(alpm_list_t *targets)
{
	alpm_list_t *i, *k, *syncdbs, *dbcache;
	pmdb_t *db;
	pmpkg_t *pkg;

	char buf[1024];
	const char *pkgname;
	char *dupstr;
	struct pkgpair pkgpair;
	void *memlist_ptr;

	struct pw_hashdb *hashdb = hashdb_new();

	db = alpm_option_get_localdb();
	ASSERT(db != NULL, return error(PW_ERR_LOCALDB_NULL));

	dbcache = alpm_db_get_pkgcache(db);
	ASSERT(dbcache != NULL, return error(PW_ERR_LOCALDB_CACHE_NULL));

	hash_packages(dbcache, hashdb->local, hashdb->local_provides, hashdb);

	syncdbs = alpm_option_get_syncdbs();
	for (i = syncdbs; i; i = i->next) {
		db = i->data;
		hash_packages(alpm_db_get_pkgcache(db), hashdb->sync, hashdb->sync_provides,
					  hashdb);
	}

	/* Do dependency crawling for target list */
	for (i = targets; i; i = i->next) {
		crawl_deps(hashdb, i->data);
	}

	hashdb_free(hashdb);
	return 0;
}
