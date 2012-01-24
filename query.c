#include <string.h>
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

/* Removes version information from a package string like "glibc>=2.12" */
static void chompversion(char *str)
{
	char *p;
	p = strchr(str, '>');
	if (p) {
		*p = 0;
		return;
	}
	p = strchr(str, '<');
	if (p) {
		*p = 0;
		return;
	}
	p = strchr(str, '=');
	if (p) {
		*p = 0;
	}
}

/* -Qi */
static int query_info(alpm_db_t *localdb, alpm_list_t *targets)
{
	int ret, hits, found, pkgcount;
	alpm_list_t *i, *k, *dbcache;
	alpm_pkg_t *pkg;

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
static int query_search(alpm_db_t *localdb, const char *pkgname)
{
	int ret, found;
	const char *repo;
	alpm_list_t *i, *k, *dbcache, *groups;
	alpm_list_t *syncdbs;
	alpm_pkg_t *pkg;

	dbcache = alpm_db_get_pkgcache(localdb);
	syncdbs = alpm_option_get_syncdbs(config->handle);

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
	alpm_db_t *localdb = alpm_option_get_localdb(config->handle);
	if (!localdb) {
		return error(PW_ERR_INIT_LOCALDB);
	}

	alpm_list_t *dblist = NULL;
	alpm_list_t *i, *j, *dbcache;
	alpm_pkg_t *pkg, *spkg;
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
		alpm_list_t *sdbs = alpm_option_get_syncdbs(config->handle);
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
 * For AUR packages, this also downloads and extracts PKGBUILD in cwd.
 * In addition, the "normalized" packages will be cached in hashdb->pkg_from
 *
 * @param curl curl handle
 * @param hashdb hash database
 * @param pkg package name
 * @param resolve_lvl level of dep resolution. RESOLVE_THOROUGH forces
 *        downloading of AUR PKGBUILDs
 *
 * returns the "normalized" package if present, NULL on failure
 */
static const char *normalize_package(CURL *curl, struct pw_hashdb *hashdb,
									 const char *pkgname, int resolve_lvl)
{
	const char *provided = NULL;
	struct pkgpair pkgpair;
	struct pkgpair *pkgptr;
	enum pkgfrom_t *pkgfrom;

	pkgpair.pkgname = pkgname;
	pkgpair.pkg = NULL;

	/* If we know where pkg is from and it's not AUR / it's from AUR and
	 * already downloaded, done */
	pkgfrom = hashmap_search(hashdb->pkg_from, (void *) pkgname);
	if (pkgfrom) {
		if (*pkgfrom != PKG_FROM_AUR ||
			hash_search(hashdb->aur_downloaded, (void *) pkgname)) {
			return pkgname;
		}

		goto search_aur;
	}

	/* If it's in local db and not AUR, done */
	if (hash_search(hashdb->local, &pkgpair)) {
		if (hash_search(hashdb->aur, &pkgpair)) {
			goto search_aur;
		}
		hashmap_insert(hashdb->pkg_from, (void *) pkgname, &hashdb->pkg_from_local);
		return pkgname;
	}

	/* Search provides cache */
	provided = hashmap_search(hashdb->provides_cache, (void *) pkgname);
	if (provided) {
		return provided;
	}

	/* Search local provides */
	pkgptr = hashbst_tree_search(hashdb->local_provides, (void *) pkgname,
								 hashdb->local, provides_search);
	if (pkgptr) {
		/* Cache in provides and pkg_from */
		hashmap_insert(hashdb->provides_cache, (void *) pkgname,
					   (void *) pkgptr->pkgname);
		hashmap_insert(hashdb->pkg_from, (void *) pkgptr->pkgname, &hashdb->pkg_from_local);
		return pkgptr->pkgname;
	}

	/* Search sync provides tree in local db
	 * TODO: Is there a meaning to this?
	 * local provides are obtained from local packages.
	 * sync provides are obtained from sync packages.
	 * So searching for sync provides in local database is kind of...
	 */
	pkgptr = hashbst_tree_search(hashdb->sync_provides, (void *) pkgname,
								 hashdb->local, provides_search);

	if (pkgptr) {
		/* Cache in pkg_from */
		hashmap_insert(hashdb->pkg_from, (void *) pkgptr->pkgname, &hashdb->pkg_from_local);
		return pkgptr->pkgname;
	}

	/* Search sync db */
	if (hash_search(hashdb->sync, &pkgpair)) {
		hashmap_insert(hashdb->pkg_from, (void *) pkgname, &hashdb->pkg_from_sync);
		return pkgname;
	}

	/* Sync provides */
	pkgptr = hashbst_tree_search(hashdb->sync_provides, (void *) pkgname,
								 hashdb->sync, provides_search);
	if (pkgptr) {
		hashmap_insert(hashdb->pkg_from, (void *) pkgptr->pkgname,
					   &hashdb->pkg_from_sync);
		hashmap_insert(hashdb->provides_cache, (void *) pkgname, (void *) pkgptr->pkgname);
		return pkgptr->pkgname;
	}

search_aur:
	pkgpair.pkgname = pkgname;
	pkgpair.pkg = NULL;

	/* For non RESOLVE_THOROUGH, don't bother downloading PKGBUILD of updated
	 * AUR packages
	 */
	if (resolve_lvl != RESOLVE_THOROUGH) {
		if (hash_search(hashdb->aur, &pkgpair) &&
			!hash_search(hashdb->aur_outdated, (void *) pkgname)) {
			goto done;
		}
	}

	/* Download and extract from AUR */
	if (dl_extract_single_package(curl, pkgname, NULL, 0)) {
		return NULL;
	}

	hash_insert(hashdb->aur_downloaded, (void *) pkgname);
	hashmap_insert(hashdb->pkg_from, (void *) pkgname, &hashdb->pkg_from_aur);

done:
	return pkgname;
}

/* Resolve dependencies for a given package
 * @param curl curl handle
 * @param hashdb hash database
 * @param curpkg current package we are resolving
 * @param dep_list pointer to list to store resulting dependencies
 * @param resolve_lvl level of dep resolution. RESOLVE_THOROUGH forces
 *        downloading of AUR PKGBUILDs
 *
 * returns -1 on error, 0 on success
 */
static int crawl_resolve(CURL *curl, struct pw_hashdb *hashdb, struct pkgpair *curpkg,
						 alpm_list_t **dep_list, int resolve_lvl)
{
	alpm_list_t *i, *depmod_list, *deps = NULL;
	struct pkgpair *pkgpair;
	struct pkgpair tmppkg;
	void *pkg_provides;
	void *memlist_ptr;
	const char *cache_result;
	const char *depname, *final_pkgname;
	char cwd[PATH_MAX];
	char buf[PATH_MAX];

	/* Normalize package before doing anything else */
	final_pkgname = normalize_package(curl, hashdb, curpkg->pkgname, resolve_lvl);
	if (!final_pkgname) {
		return -1;
	}

	enum pkgfrom_t *from = hashmap_search(hashdb->pkg_from, (void *) final_pkgname);
	if (!from) {
		die("Failed to find out where package \"%s\" is from!\n", final_pkgname);
	}

	switch (*from) {
	case PKG_FROM_LOCAL:
		tmppkg.pkgname = final_pkgname;
		pkgpair = hash_search(hashdb->local, &tmppkg);
		goto get_deps;
	case PKG_FROM_SYNC:
		tmppkg.pkgname = final_pkgname;
		pkgpair = hash_search(hashdb->sync, &tmppkg);
		goto get_deps;
	default:
		goto aur_deps;
	}

aur_uptodate:
	tmppkg.pkgname = final_pkgname;
	tmppkg.pkg = NULL;
	pkgpair = hash_search(hashdb->aur, &tmppkg);

get_deps:
	if (!pkgpair) {
		/* Shouldn't happen */
		die("Unable to find package \"%s\" in local/sync db!", final_pkgname);
	}

	depmod_list = alpm_pkg_get_depends(pkgpair->pkg);
	for (i = depmod_list; i; i = i->next) {
		char *s = alpm_dep_compute_string(i->data);
		strncpy(buf, s, sizeof(buf));
		free(s);
		chompversion(buf);
		depname = normalize_package(curl, hashdb, buf, resolve_lvl);
		/* Possibility of normalize_package fail due to AUR download failing */
		if (!depname) {
			alpm_list_free(deps);
			return -1;
		}
		deps = alpm_list_add(deps, (void *) depname);
	}

	if (dep_list) {
		*dep_list = deps;
	} else {
		alpm_list_free(deps);
	}

	return 0;

aur_deps:
	tmppkg.pkgname = final_pkgname;
	tmppkg.pkg = NULL;

	/* For installed AUR packages which are up to date */
	if (resolve_lvl != RESOLVE_THOROUGH) {
		if (hash_search(hashdb->aur, &tmppkg) &&
			!hash_search(hashdb->aur_outdated, (void *) final_pkgname)) {
			/* NOTE: top goto ! */
			goto aur_uptodate;
		}
	}

	/* RESOLVE_THOROUGH / out to date AUR package.
	 * Download pkgbuild and extract deps */
	if (!getcwd(cwd, PATH_MAX)) {
		return error(PW_ERR_GETCWD);
	}

	if (chdir(final_pkgname)) {
		return error(PW_ERR_CHDIR);
	}

	deps = grab_dependencies("PKGBUILD");
	if (chdir(cwd)) {
		alpm_list_free(deps);
		return error(PW_ERR_RESTORECWD);
	}

	if (dep_list) {
		const char *normdep;
		alpm_list_t *new_deps = NULL;

		/* Transfer control to memlist and normalize packages */
		for (i = deps; i; i = i->next) {
			memlist_ptr = memlist_add(hashdb->strpool, &i->data);
			normdep = normalize_package(curl, hashdb, memlist_ptr, resolve_lvl);
			new_deps = alpm_list_add(new_deps, (void *) normdep);
		}

		*dep_list = new_deps;
	}

	alpm_list_free(deps);
	return 0;
}

/* Depending on resolve_lvl, decide if we want to continue resolving the package
 *
 * If resolve_lvl is RESOLVE_IMMEDIATE, only out of date / new AUR packages
 * will be resolved.
 */
static void should_we_continue_resolving(CURL *curl,
										 struct pw_hashdb *hashdb,
										 struct stack *st,
										 struct pkgpair *deppkg,
										 int resolve_lvl)
{
	if (resolve_lvl == RESOLVE_THOROUGH) {
		stack_push(st, deppkg);
		return;
	}

	/* RESOLVE_IMMEDIATE. Normalize package. Only continue resolving
	 * new AUR packages
	 */
	const char *pkgname;
	pkgname = normalize_package(curl, hashdb, deppkg->pkgname, resolve_lvl);

	/* Continue resolving for new AUR packages */
	if (hash_search(hashdb->aur_downloaded, (void *) pkgname)) {
		stack_push(st, deppkg);
	}
}

/* Adds immediate dependencies to hashdb->immediate_deps
 * This enables us to print the immediate deps instead of the entire huge
 * dependency graph.
 */
static void add_immediate_deps(struct pw_hashdb *hashdb, const char *pkgname,
							   alpm_list_t *deps, struct hash_table *immediate)
{
	enum pkgfrom_t *from = NULL;
	alpm_list_t *i;
	struct pkgpair pkgpair;

	from = hashmap_search(hashdb->pkg_from, (void *) pkgname);
	if (!from) {
		return;
	} else if (*from == PKG_FROM_AUR) {
		pkgpair.pkgname = pkgname;
		/* Applies to new AUR packages and outdated AUR packages */
		if (!hash_search(hashdb->aur, &pkgpair) ||
			hash_search(hashdb->aur_outdated, (void *) pkgname)) {
			for (i = deps; i; i = i->next) {
				if (!hash_search(immediate, (void *) i->data)) {
					hashdb->immediate_deps = alpm_list_add(hashdb->immediate_deps, i->data);
					hash_insert(immediate, (void *) i->data);
				}
			}
		}
	}
}

void build_dep_graph(struct graph **graph, struct pw_hashdb *hashdb,
					 alpm_list_t *targets, int resolve_lvl)
{
	if (!graph) {
		return;
	}

	if (!*graph) {
		*graph = graph_new((pw_hash_fn) sdbm, (pw_hashcmp_fn) strcmp);
	}

	struct stack *st = stack_new(sizeof(struct pkgpair));
	struct hash_table *resolved = hash_new(HASH_TABLE, (pw_hash_fn) sdbm,
										   (pw_hashcmp_fn) strcmp);
	struct hash_table *immediate = hash_new(HASH_TABLE, (pw_hash_fn) sdbm,
											(pw_hashcmp_fn) strcmp);
	int ret;
	struct pkgpair pkgpair, deppkg;
	alpm_list_t *i;
	alpm_list_t *deps;

	CURL *curl;
	curl = curl_easy_new();
	if (!curl) {
		error(PW_ERR_CURL_INIT);
		return;
	}

	/* Push all packages down stack */
	for (i = targets; i; i = i->next) {
		pkgpair.pkgname = i->data;
		pkgpair.pkg = NULL;
		stack_push(st, &pkgpair);
	}

	while (!stack_empty(st)) {
		stack_pop(st, &pkgpair);
		deps = NULL;

		if (hash_search(resolved, (void *) pkgpair.pkgname)) {
			goto cleanup_deps;
		}

		ret = crawl_resolve(curl, hashdb, &pkgpair, &deps, resolve_lvl);
		if (ret) {
			pw_fprintf(PW_LOG_ERROR, stderr, "Error in resolving packages.\n");
			goto cleanup;
		}

		for (i = deps; i; i = i->next) {
			deppkg.pkgname = i->data;
			deppkg.pkg = NULL;

			/* immediate vs thorough resolve */
			should_we_continue_resolving(curl, hashdb, st, &deppkg, resolve_lvl);

			/* dep --> current */
			graph_add_edge(*graph, i->data, (void *) pkgpair.pkgname);
		}

		hash_insert(resolved, (void *) pkgpair.pkgname);

		/* Add immediate dependencies, for pretty printing purposes */
		add_immediate_deps(hashdb, pkgpair.pkgname, deps, immediate);
cleanup_deps:
		alpm_list_free(deps);
	}

cleanup:
	hash_free(resolved);
	hash_free(immediate);
	stack_free(st);
	curl_easy_cleanup(curl);
}

void print_topo_order(struct graph *graph, struct stack *topost)
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

int powaur_crawl(alpm_list_t *targets)
{
	int ret = 0;
	char cwd[PATH_MAX];
	if (!getcwd(cwd, PATH_MAX)) {
		return error(PW_ERR_GETCWD);
	}

	if (chdir(powaur_dir)) {
		return error(PW_ERR_CHDIR, powaur_dir);
	}

	struct pw_hashdb *hashdb = build_hashdb();
	if (!hashdb) {
		pw_fprintf(PW_LOG_ERROR, stderr, "Unable to build hash database!\n");
		ret = -1;
	}

	alpm_list_t *i, *target_pkgs;
	struct graph *graph;
	struct stack *topost = stack_new(sizeof(int));
	int have_cycles;
	for (i = targets; i; i = i->next) {
		stack_reset(topost);
		graph = NULL;
		target_pkgs = alpm_list_add(NULL, i->data);
		build_dep_graph(&graph, hashdb, target_pkgs, RESOLVE_THOROUGH);
		if (have_cycles) {
			printf("Cyclic dependencies for package \"%s\"\n", i->data);
		}

		graph_toposort(graph, topost);
		if (stack_empty(topost)) {
			printf("Package \"%s\" has no dependencies.\n", i->data);
		} else {
			printf("\n");
			pw_printf(PW_LOG_INFO, "\"%s\" topological order: ", i->data);
			print_topo_order(graph, topost);
		}

		graph_free(graph);
		alpm_list_free(target_pkgs);
	}

	stack_free(topost);
	hashdb_free(hashdb);

	if (chdir(cwd)) {
		return error(PW_ERR_RESTORECWD);
	}
	return ret;
}

void pkgpair_print_color(void *p)
{
	struct pkgpair *pkgpair_ptr = p;
	printf("%s%s%s %s%s%s\n", color.bold, pkgpair_ptr->pkgname, color.nocolor,
		   color.bgreen, alpm_pkg_get_version(pkgpair_ptr->pkg), color.nocolor);
}

int powaur_list_aur(void)
{
	struct pw_hashdb *hashdb = build_hashdb();
	if (!hashdb) {
		pw_fprintf(PW_LOG_ERROR, stderr, "Failed to build hashdb!\n");
		return -1;
	}

	hash_walk(hashdb->aur, pkgpair_print_color);
	hashdb_free(hashdb);
	return 0;
}
