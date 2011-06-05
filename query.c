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

/* @param hash hash table of struct pkgpair * with pmpkg_t * as pkg field
 * @param provides hashmap of provided packages
 * @param pkgname package name
 * @param verbose switch on detailed dependency resolution
 */
static alpm_list_t *crawl_resolve(struct hash_table *hash, struct hashmap *local_provides,
								  struct hashmap *sync_provides, struct pkgpair *curpkg,
								  int verbose)
{
	alpm_list_t *i, *depmod_list, *deps = NULL;
	void *pkg_provides;
	struct pkgpair *pkgpair = hash_search(hash, curpkg);
	/* If the pkg can be found in a db, just skip for non-verbose */
	if (!verbose && pkgpair) {
		return NULL;
	}

	if (!pkgpair) {
		goto search_provides;
	}

	/* pkg is installed, return its deps */
	depmod_list = alpm_pkg_get_depends(pkgpair->pkg);
	for (i = depmod_list; i; i = i->next) {
		deps = alpm_list_add(deps, (void *) alpm_dep_get_name(i->data));
	}

	return deps;

search_provides:
	/* Search local provides */
	pkgpair = hashmap_tree_search(local_provides, (void *) curpkg->pkgname, hash, provides_search);
	if (pkgpair) {
		return alpm_list_add(NULL, (void *) pkgpair->pkgname);
	}

	/* Search sync provides */
	pkgpair = hashmap_tree_search(local_provides, (void *) curpkg->pkgname, hash, provides_search);

	if (pkgpair) {
		return alpm_list_add(NULL, (void *) pkgpair->pkgname);
	}

	printf("%s is an AUR package\n", (void *) curpkg->pkgname);
	return NULL;
}

static void crawl_deps(struct hash_table *hash, struct hashmap *local_provides,
					   struct hashmap *sync_provides, const char *pkgname)
{
	struct graph *graph = graph_new((pw_hash_fn) sdbm, (pw_hashcmp_fn) strcmp);
	struct stack *st = stack_new(sizeof(struct pkgpair));

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
		deps = crawl_resolve(hash, local_provides, sync_provides, &pkgpair, 1);

		for (i = deps; i; i = i->next) {
			deppkg.pkgname = i->data;
			deppkg.pkg = NULL;
			stack_push(st, &deppkg);

			/* dep --> current */
			graph_add_edge(graph, i->data, (void *) pkgpair.pkgname);
		}

		alpm_list_free(deps);
	}

	struct stack *topost = stack_new(sizeof(int));
	int cycles = graph_toposort(graph, topost);
	if (cycles) {
		goto cleanup;
	}

	if (stack_empty(topost)) {
		printf("%s has no dependencies.\n", pkgname);
		goto cleanup;
	}

	int idx;
	int cnt = 0;
	printf("Topological order: ");
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
 * @param provides hashmap used to hash provides
 * @param strpool memlist of dynamically allocated strings
 * @param pkgpool memlist of struct pkgpair
 */
static void hash_packages(alpm_list_t *dbcache, struct hash_table *htable,
						  struct hashmap *provides, struct memlist *strpool,
						  struct memlist *pkgpool)
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
		memlist_ptr = memlist_add(pkgpool, &pkgpair);
		hash_insert(htable, memlist_ptr);

		/* Provides */
		for (k = alpm_pkg_get_provides(pkg); k; k = k->next) {
			snprintf(buf, 1024, "%s", k->data);
			if (!strtrim_ver(buf)) {
				continue;
			}

			dupstr = xstrdup(buf);
			memlist_ptr = memlist_add(strpool, &dupstr);
			hashmap_insert(provides, memlist_ptr, (void *) pkgname);
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

	struct memlist *depstrs = memlist_new(4096, sizeof(char *), 1);
	struct memlist *pkgstore = memlist_new(4096, sizeof(struct pkgpair), 0);

	struct hash_table *hash = hash_new(HASH_TABLE, pkg_sdbm, pkg_cmp);
	struct hashmap *local_provides = hashmap_new((pw_hash_fn) sdbm, (pw_hashcmp_fn) strcmp);
	struct hashmap *sync_provides = hashmap_new((pw_hash_fn) sdbm, (pw_hashcmp_fn) strcmp);

	db = alpm_option_get_localdb();
	ASSERT(db != NULL, return error(PW_ERR_LOCALDB_NULL));

	dbcache = alpm_db_get_pkgcache(db);
	ASSERT(dbcache != NULL, return error(PW_ERR_LOCALDB_CACHE_NULL));

	hash_packages(dbcache, hash, local_provides, depstrs, pkgstore);

	syncdbs = alpm_option_get_syncdbs();
	for (i = syncdbs; i; i = i->next) {
		db = i->data;
		hash_packages(alpm_db_get_pkgcache(db), hash, sync_provides, depstrs, pkgstore);
	}

	/* Do dependency crawling for target list */
	for (i = targets; i; i = i->next) {
		crawl_deps(hash, local_provides, sync_provides, i->data);
	}

	memlist_free(depstrs);
	memlist_free(pkgstore);
	hashmap_free(local_provides);
	hashmap_free(sync_provides);
	hash_free(hash);
	return 0;
}
