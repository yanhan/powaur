#ifndef POWAUR_PACKAGE_H
#define POWAUR_PACKAGE_H

#include <stdio.h>
#include <alpm.h>

#include "hashdb.h"
#include "powaur.h"

struct aurpkg_t {
	char *id;
	char *name;
	char *version;
	char *category;
	char *desc;
	char *url;
	char *urlpath;
	char *license;
	int votes;
	int outofdate;

	/* list of char* */
	alpm_list_t *arch;
	alpm_list_t *depends;
	alpm_list_t *optdepends;
	alpm_list_t *conflicts;
	alpm_list_t *provides;
	alpm_list_t *replaces;
};

struct aurpkg_t *aurpkg_new(void);
void aurpkg_free(struct aurpkg_t *pkg);
int aurpkg_name_cmp(const void *a, const void *b);
int aurpkg_vote_cmp(const void *a, const void *b);

void parse_pkgbuild(struct aurpkg_t *pkg, FILE *fp, int preserve_version);

/* Returns the list of char * of dependencies specified in pkgbuild
 * The list and its contents must be freed by the caller
 */
alpm_list_t *grab_dependencies(const char *pkgbuild);

/* Resolve dependencies for powaur_get
 * returns the list of strings of unresolved packages. The list and strings
 * are to be freed by the caller.
 */
alpm_list_t *resolve_dependencies(struct pw_hashdb *hashdb, alpm_list_t *packages);

/* Returns a statically allocated string indicating wich db the pkg came from */
const char *which_db(alpm_list_t *sdbs, const char *pkgname, alpm_list_t **grp);

/* Prints pretty pkg, for plain -Q, -Qs, -Ss */
void print_pkg_pretty(alpm_list_t *sdbs, pmpkg_t *pkg, enum dumplvl_t lvl);

/* Dumps entire pacman database, for -Q, -Qi, -Qs, -Si, -Ss w/o targets */
int pacman_db_dump(enum pkgfrom_t from, enum dumplvl_t lvl);

/* Dumps a pacman package, for -Qi and -Si */
void pacman_pkgdump(pmpkg_t *pkg, enum pkgfrom_t from);

#endif
