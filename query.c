#include <alpm.h>

#include "conf.h"
#include "environment.h"
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
	alpm_list_t *i, *k, *dbcache, *groups;
	pmpkg_t *pkg;

	dbcache = alpm_db_get_pkgcache(localdb);

	for (k = dbcache; k; k = k->next) {
		pkg = k->data;
		if (!strcmp(pkgname, alpm_pkg_get_name(pkg))) {
			printf("local/%s %s", pkgname, alpm_pkg_get_version(pkg));

			groups = alpm_pkg_get_groups(pkg);

			/* Just print the 1st group */
			if (groups) {
				printf(" (%s)", groups->data);
			}

			printf("\n%s %s\n", comstrs.tab, alpm_pkg_get_desc(pkg));
			found = 1;
		}
	}

	return found ? 0: -1;
}

int powaur_query(alpm_list_t *targets)
{
	pmdb_t *localdb = alpm_option_get_localdb();
	ASSERT(localdb != NULL, RET_ERR(PW_ERR_INIT_LOCAL_DB, -1));

	alpm_list_t *dblist = NULL;
	alpm_list_t *i, *j, *dbcache;
	pmpkg_t *pkg;
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
			ret = pacman_db_info(dblist, PKG_FROM_LOCAL, 0);
		} else if (config->op_q_search) {
			ret = pacman_db_info(dblist, PKG_FROM_LOCAL, 1);
		} else {
			for (i = alpm_db_get_pkgcache(localdb); i; i = i->next) {
				pkg = i->data;
				printf("%s %s\n", alpm_pkg_get_name(pkg),
					   alpm_pkg_get_version(pkg));
			}
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
		dbcache = alpm_db_get_pkgcache(localdb);

		for (i = targets; i; i = i->next) {
			found = 0;
			for (j = dbcache; j; j = j->next) {
				pkg = j->data;
				if (!strcmp(i->data, alpm_pkg_get_name(pkg))) {
					printf("%s %s\n", i->data, alpm_pkg_get_version(pkg));

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
