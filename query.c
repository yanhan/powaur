#include <alpm.h>

#include "conf.h"
#include "curl.h"
#include "environment.h"
#include "graph.h"
#include "hashdb.h"
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

struct graph *build_dep_graph(struct pw_hashdb *hashdb, const char *pkgname,
							  struct stack *topost, int *cycles)
{
	struct graph *graph = graph_new((pw_hash_fn) sdbm, (pw_hashcmp_fn) strcmp);
	struct stack *st = stack_new(sizeof(struct pkgpair));
	struct hash_table *resolved = hash_new(HASH_TABLE, (pw_hash_fn) sdbm,
										   (pw_hashcmp_fn) strcmp);

	int ret;
	struct pkgpair pkgpair, deppkg;
	alpm_list_t *i;
	alpm_list_t *deps;

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

	if (!topost) {
		goto cleanup;
	}

	int have_cycles = graph_toposort(graph, topost);
	if (cycles) {
		*cycles = have_cycles;
	}

cleanup:
	stack_free(st);
	hash_free(resolved);
	curl_easy_cleanup(curl);

	return graph;
}

static void print_topo_order(struct graph *graph, struct stack *topost)
{
	int idx;
	int cnt = 0;
	const char *curpkg;

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

int powaur_crawl(alpm_list_t *targets)
{
	struct pw_hashdb *hashdb = build_hashdb();
	if (!hashdb) {
		pw_fprintf(PW_LOG_ERROR, stderr, "Unable to build hash database!\n");
		return -1;
	}

	alpm_list_t *i;
	struct graph *graph;
	struct stack *topost = stack_new(sizeof(int));
	int have_cycles;
	for (i = targets; i; i = i->next) {
		stack_reset(topost);
		graph = build_dep_graph(hashdb, i->data, topost, &have_cycles);
		if (have_cycles) {
			printf("Cyclic dependencies for package \"%s\"\n", i->data);
		} else if (stack_empty(topost)) {
			printf("Package \"%s\" has no dependencies\n", i->data);
		} else {
			printf("\n");
			pw_printf(PW_LOG_INFO, "\"%s\" topological order: ", i->data);
			print_topo_order(graph, topost);
		}

		graph_free(graph);
	}

	stack_free(topost);
	hashdb_free(hashdb);
	return 0;
}
