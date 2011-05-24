#ifndef POWAUR_PACKAGE_H
#define POWAUR_PACKAGE_H

#include <stdio.h>

#include <alpm.h>

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

/* Used in sync */
struct pkginfo_t {
	char *name;
	char *version;
	time_t install_date;
};

struct aurpkg_t *aurpkg_new(void);
void aurpkg_free(struct aurpkg_t *pkg);
int aurpkg_name_cmp(const void *a, const void *b);
int aurpkg_vote_cmp(const void *a, const void *b);

struct pkginfo_t *pkginfo_new(const char *name, const char *ver, time_t d);
void pkginfo_free(struct pkginfo_t *info);
void pkginfo_free_all(struct pkginfo_t *info);
int pkginfo_cmp(const void *a, const void *b);
int pkginfo_name_cmp(const void *a, const void *b);
int pkginfo_ver_cmp(const void *a, const void *b);
int pkginfo_date_cmp(const void *a, const void *b);
int pkginfo_mod_cmp(const void *a, const void *b);

void parse_pkgbuild(struct aurpkg_t *pkg, FILE *fp);

alpm_list_t *resolve_dependencies(alpm_list_t *packages);

int pacman_db_info(alpm_list_t *dbs, enum pkgfrom_t from, int search);
void pacman_pkgdump(pmpkg_t *pkg, enum pkgfrom_t from);

#endif
