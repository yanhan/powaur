#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <alpm.h>

#include "environment.h"
#include "package.h"
#include "powaur.h"
#include "util.h"

struct aurpkg_t *aurpkg_new(void)
{
	struct aurpkg_t *pkg;
	CALLOC(pkg, 1, sizeof(struct aurpkg_t), RET_ERR(PW_ERR_MEMORY, NULL));
	return pkg;
}

void aurpkg_free(struct aurpkg_t *pkg)
{
	if (!pkg) {
		return;
	}

	FREE(pkg->id);
	FREE(pkg->name);
	FREE(pkg->version);
	FREE(pkg->category);
	FREE(pkg->desc);
	FREE(pkg->url);
	FREE(pkg->urlpath);
	FREE(pkg->license);

	FREELIST(pkg->arch);
	FREELIST(pkg->conflicts);
	FREELIST(pkg->provides);
	FREELIST(pkg->depends);
	FREELIST(pkg->optdepends);
	FREELIST(pkg->replaces);

	free(pkg);
}

int aurpkg_name_cmp(const void *a, const void *b)
{
	return strcmp(((const struct aurpkg_t *)a)->name,
				  ((const struct aurpkg_t *)b)->name);
}

int aurpkg_vote_cmp(const void *a, const void *b)
{
	return ((const struct aurpkg_t *)b)->votes -
		   ((const struct aurpkg_t *)a)->votes;
}

struct pkginfo_t *pkginfo_new(const char *name, const char *ver, time_t d)
{
	struct pkginfo_t *info;
	CALLOC(info, 1, sizeof(struct pkginfo_t), RET_ERR(PW_ERR_MEMORY,NULL));

	info->name = strdup(name);
	info->version = strdup(ver);
	info->install_date = d;
	return info;
}

void pkginfo_free(struct pkginfo_t *info)
{
	if (info) {
		free(info);
	}
}

void pkginfo_free_all(struct pkginfo_t *info)
{
	if (info) {
		free(info->name);
		free(info->version);
		free(info);
	}
}

int pkginfo_cmp(const void *a, const void *b)
{
	const struct pkginfo_t *left = a;
	const struct pkginfo_t *right = b;
	int ret;

	ret = strcmp(left->name, right->name);
	if (ret) {
		return ret;
	}

	ret = alpm_pkg_vercmp(left->version, right->version);
	if (ret) {
		return ret;
	}

	return left->install_date  - right->install_date;
}

int pkginfo_name_cmp(const void *a, const void *b)
{
	return strcmp(((const struct pkginfo_t *)a)->name,
				  ((const struct pkginfo_t *)b)->name);
}

int pkginfo_ver_cmp(const void *a, const void *b)
{
	return alpm_pkg_vercmp(((const struct pkginfo_t *)a)->version,
						   ((const struct pkginfo_t *)b)->version);
}

int pkginfo_date_cmp(const void *a, const void *b)
{
	return ((const struct pkginfo_t *)a)->install_date -
		   ((const struct pkginfo_t *)b)->install_date;
}

/* Returns 0 if pkgs are the same but modified in version / install date */
int pkginfo_mod_cmp(const void *a, const void *b)
{
	const struct pkginfo_t *left = a;
	const struct pkginfo_t *right = b;
	int ret;

	if (strcmp(left->name, right->name)) {
		return -1;
	}

	ret = alpm_pkg_vercmp(left->version, right->version);
	if (ret) {
		return 0;
	} else {
		return !(left->install_date - right->install_date);
	}
}

/* Parse multi-line bash array.
 *
 * NOTE: Make sure the array elements are delimited by single quotes.
 *
 * This function does quite a bit of error checking,
 * it can even handle arrays like:
 *
 * depends    =
 * (
 * 'packageA'
 * 		'packageB'   'packageC' 'packageD'
 * 'packageE'
 * )
 *
 * So if it really barfs on smth, pls check the PKGBUILD.
 */

/* TODO: Handle ">=" for depends, provides, etc, ":" for optdepends
 * Refer to https://wiki.archlinux.org/index.php/Pkgbuild for details.
 */
static void parse_bash_array(alpm_list_t **list, FILE *fp,
							 char *buf, char *line)
{
	static const char *delim = "'";

	int len;
	int can_break = 0;
	char *token, *saveptr;

	/* Parse multi-line bash array */
	for (; !feof(fp) && !can_break; line = fgets(buf, PATH_MAX, fp)) {

		line = strtrim(line);
		len = strlen(line);

		if (len == 0) {
			continue;
		} else if (line[len - 1] == ')') {
			can_break = 1;
		}

		for (token = strtok_r(line, delim, &saveptr), line = NULL;
			 token; token = strtok_r(line, delim, &saveptr)) {
			
			token = strtrim(token);
			if (strlen(token) == 0) {
				continue;
			} else if (token[0] == ')') {
				can_break = 1;
				break;
			}

			*list = alpm_list_add(*list, strdup(token));

			pw_printf(PW_LOG_DEBUG, "    Parsed \"%s\"\n", token);
		}
	}
}

#define PARSE_BASH_ARRAY_PREAMBLE(myline) \
	while (1) {\
		myline = strchr(myline, '(');\
		if (myline) {\
			++myline;\
			break;\
		}\
		myline = fgets(buf, PATH_MAX, fp);\
		if (feof (fp)) break;\
		myline = strtrim(myline);\
	}\

/* Obtain deps, optional deps, conflicts, replaces from fp */
void parse_pkgbuild(struct aurpkg_t *pkg, FILE *fp)
{
	char buf[PATH_MAX];
	char *line;

	char *token;
	char *saveptr;

	while (line = fgets(buf, PATH_MAX, fp)) {
		line = strtrim(line);

		if (line == NULL || strlen(line) == 0) {
			continue;
		}

		if (!strncmp(line, "depends", 7)) {
			pw_printf(PW_LOG_DEBUG, "Parsing PKGBUILD depends\n");

			PARSE_BASH_ARRAY_PREAMBLE(line);
			parse_bash_array(&(pkg->depends), fp, buf, line);

		} else if (!strncmp(line, "provides", 8)) {
			pw_printf(PW_LOG_DEBUG, "Parsing PKGBUILD provides\n");

			PARSE_BASH_ARRAY_PREAMBLE(line);
			parse_bash_array(&(pkg->provides), fp, buf, line);

		} else if (!strncmp(line, "conflicts", 9)) {
			pw_printf(PW_LOG_DEBUG, "Parsing PKGBUILD conflicts\n");

			PARSE_BASH_ARRAY_PREAMBLE(line);
			parse_bash_array(&(pkg->conflicts), fp, buf, line);
		} else if (!strncmp(line, "replaces", 8)) {
			pw_printf(PW_LOG_DEBUG, "Parsing PKGBUILD replaces\n");

			PARSE_BASH_ARRAY_PREAMBLE(line);
			parse_bash_array(&(pkg->replaces), fp, buf, line);
		} else if (!strncmp(line, "arch", 4)) {
			pw_printf(PW_LOG_DEBUG, "Parsing PKGBUILD architectures\n");

			PARSE_BASH_ARRAY_PREAMBLE(line);
			parse_bash_array(&(pkg->arch), fp, buf, line);
		} else if (!strncmp(line, "build", 5)) {
			break;
		}
	}
}

/* Akin to yaourt -Si / pacman -Si.
 * Dumps info from sync dbs and returns.
 */
int pacman_db_info(alpm_list_t *dbs, enum pkgfrom_t from, int search)
{
	int cnt = 0;
	alpm_list_t *i, *j;

	pmdb_t *db;
	pmpkg_t *pkg;
	pmdepend_t *dep;

	if (search) {
		for (i = dbs; i; i = i->next) {
			db = i->data;
			for (j = alpm_db_get_pkgcache(db); j; j = j->next) {
				pkg = j->data;
				printf("%s/%s %s\n%s%s\n", alpm_db_get_name(db),
					   alpm_pkg_get_name(pkg), alpm_pkg_get_version(pkg),
					   comstrs.tab, alpm_pkg_get_desc(pkg));
			}
		}

		return 0;
	}

	for (i = dbs; i; i = i->next) {
		db = i->data;

		for (j = alpm_db_get_pkgcache(db); j; j = j->next) {
			if (cnt++) {
				printf("\n");
			}

			pkg = j->data;
			pacman_pkgdump(pkg, from);
		}
	}
}

static void humanize_size(off_t sz, const char *prefix)
{
	static const char *units = "BKMG";
	int ptr = 0;
	off_t rem, quo, div = 1;

	for (quo = sz; quo > 9999 && ptr < 3; ++ptr) {
		div *= 1024;
		quo = sz / div;
		rem = sz % div;
	}

	while (rem > 1000) {
		div = rem % 10;
		rem /= 10;
		rem += div >= 5 ? 1 : 0;
	}

	if (rem > 100) {
		rem /= 10;
	}

	printf("%s %-u.%u %c\n", prefix, quo, rem, units[ptr]);
}

void pacman_pkgdump(pmpkg_t *pkg, enum pkgfrom_t from)
{
	static const char *datefmt = "%a %d %b %Y %I:%M:%S %p %Z";

	alpm_list_t *i, *results = NULL;
	pmdb_t *db;
	pmdepend_t *dep;
	pmpkgreason_t reason;

	int has_script;
	time_t inst_time;
	struct tm tm_st;

	char installdate[60];
	char builddate[60];

	db = alpm_pkg_get_db(pkg);
	if (!db) {
		return;
	}

	memset(&tm_st, 0, sizeof(struct tm));
	inst_time = alpm_pkg_get_builddate(pkg);
	localtime_r(&inst_time, &tm_st);
	strftime(builddate, 60, datefmt, &tm_st);

	/* Local pkg specific */
	if (from == PKG_FROM_LOCAL) {
		has_script = alpm_pkg_has_scriptlet(pkg);
		reason = alpm_pkg_get_reason(pkg);

		memset(&tm_st, 0, sizeof(struct tm));
		inst_time = alpm_pkg_get_installdate(pkg);
		localtime_r(&inst_time, &tm_st);
		strftime(installdate, 60, datefmt, &tm_st);
	}

	if (from == PKG_FROM_SYNC) {
		printf("%s %s\n", comstrs.i_repo, alpm_db_get_name(db));
	}

	printf("%s %s\n", comstrs.i_name, alpm_pkg_get_name(pkg));
	printf("%s %s\n", comstrs.i_version, alpm_pkg_get_version(pkg));
	printf("%s %s\n", comstrs.i_url, alpm_pkg_get_url(pkg));

	print_list(alpm_pkg_get_licenses(pkg), comstrs.i_licenses);
	print_list(alpm_pkg_get_groups(pkg), comstrs.i_grps);
	print_list(alpm_pkg_get_provides(pkg), comstrs.i_provides);

	print_list_deps(alpm_pkg_get_depends(pkg), comstrs.i_deps);
	print_list_break(alpm_pkg_get_optdepends(pkg), comstrs.i_optdeps);

	if (from == PKG_FROM_LOCAL) {
		results = alpm_pkg_compute_requiredby(pkg);
		print_list(results, comstrs.i_reqby);
	}

	print_list(alpm_pkg_get_conflicts(pkg), comstrs.i_conflicts);
	print_list(alpm_pkg_get_replaces(pkg), comstrs.i_replaces);

	if (from == PKG_FROM_SYNC) {
		humanize_size(alpm_pkg_get_size(pkg), comstrs.i_dlsz);
	}

	humanize_size(alpm_pkg_get_isize(pkg), comstrs.i_isz);
	printf("%s %s\n", comstrs.i_pkger, alpm_pkg_get_packager(pkg));
	printf("%s %s\n", comstrs.i_arch, alpm_pkg_get_arch(pkg));
	printf("%s %s\n", comstrs.i_builddate, builddate);

	if (from == PKG_FROM_LOCAL) {
		printf("%s %s\n", comstrs.i_installdate, installdate);

		printf("%s ", comstrs.i_reason);
		switch (reason) {
		case PM_PKG_REASON_EXPLICIT:
			printf("Explicitly installed");
			break;
		case PM_PKG_REASON_DEPEND:
			printf("Installed as a dependency for another package");
			break;
		default:
			printf("Unknown");
			break;
		}

		printf("\n");
		printf("%s %s\n", comstrs.i_script, has_script ? "Yes" : "No");
	}

	if (from == PKG_FROM_SYNC) {
		printf("%s %s\n", comstrs.i_md5, alpm_pkg_get_md5sum(pkg));
	}

	printf("%s %s\n", comstrs.i_desc, alpm_pkg_get_desc(pkg));
	FREELIST(results);
}
