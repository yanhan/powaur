#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

#include <alpm.h>

#include "environment.h"
#include "hashdb.h"
#include "package.h"
#include "powaur.h"
#include "util.h"
#include "wrapper.h"

struct aurpkg_t *aurpkg_new(void)
{
	struct aurpkg_t *pkg;
	pkg = xcalloc(1, sizeof(struct aurpkg_t));
	return pkg;
}

void aurpkg_free(struct aurpkg_t *pkg)
{
	if (!pkg) {
		return;
	}

	free(pkg->id);
	free(pkg->name);
	free(pkg->version);
	free(pkg->category);
	free(pkg->desc);
	free(pkg->url);
	free(pkg->urlpath);
	free(pkg->license);

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

/* TODO: Return version string for >=, >, <, =.
 * TODO: provides, etc, ":" for optdepends
 * Refer to https://wiki.archlinux.org/index.php/Pkgbuild for details.
 */
static void parse_bash_array(alpm_list_t **list, FILE *fp,
							 char *buf, char *line, int preserve_ver)
{
	static const char *delim = " ";

	int len;
	int can_break = 0;
	char *token, *saveptr;
	char *tmpstr;

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

			/* Remove quotes */
			if (token[0] == '\'' || token[0] == '"') {
				++token;
			}

			len = strlen(token);
			if (token[len-1] == '\'' || token[len-1] == '"') {
				token[len-1] = 0;
				--len;
			}

			if (len == 0) {
				continue;
			} else if (token[0] == ')') {
				can_break = 1;
				break;
			} else if (token[len-1] == ')') {
				can_break = 1;
				token[len-1] = 0;
				token = strtrim(token);

				if (token[len-2] == '\'' || token[len-2] == '"') {
					token[len-2] = 0;
				}
			}

			if (preserve_ver) {
				*list = alpm_list_add(*list, strdup(token));
			} else {
				token = strtrim_ver(token);
				*list = alpm_list_add(*list, strdup(token));
			}

			pw_printf(PW_LOG_DEBUG, "%sParsed \"%s\"\n", TAB, token);
		}
	}
}

#define PARSE_BASH_ARRAY_PREAMBLE(myline)                            \
	while (1) {                                                      \
		myline = strchr(myline, '(');                                \
		if (myline) {                                                \
			++myline;                                                \
			break;                                                   \
		}                                                            \
		myline = fgets(buf, PATH_MAX, fp);                           \
		if (feof (fp)) break;                                        \
		myline = strtrim(myline);                                    \
	}                                                                \

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
			parse_bash_array(&(pkg->depends), fp, buf, line, 1);

		} else if (!strncmp(line, "provides", 8)) {
			pw_printf(PW_LOG_DEBUG, "Parsing PKGBUILD provides\n");

			PARSE_BASH_ARRAY_PREAMBLE(line);
			parse_bash_array(&(pkg->provides), fp, buf, line, 1);

		} else if (!strncmp(line, "conflicts", 9)) {
			pw_printf(PW_LOG_DEBUG, "Parsing PKGBUILD conflicts\n");

			PARSE_BASH_ARRAY_PREAMBLE(line);
			parse_bash_array(&(pkg->conflicts), fp, buf, line, 1);
		} else if (!strncmp(line, "replaces", 8)) {
			pw_printf(PW_LOG_DEBUG, "Parsing PKGBUILD replaces\n");

			PARSE_BASH_ARRAY_PREAMBLE(line);
			parse_bash_array(&(pkg->replaces), fp, buf, line, 1);
		} else if (!strncmp(line, "arch", 4)) {
			pw_printf(PW_LOG_DEBUG, "Parsing PKGBUILD architectures\n");

			PARSE_BASH_ARRAY_PREAMBLE(line);
			parse_bash_array(&(pkg->arch), fp, buf, line, 1);
		} else if (!strncmp(line, "build", 5)) {
			break;
		}
	}
}

/* Returns the list of char * of dependencies specified in pkgbuild
 * The list and its contents must be freed by the caller
 */
alpm_list_t *grab_dependencies(const char *pkgbuild)
{
	alpm_list_t *ret = NULL;
	FILE *fp;
	char buf[PATH_MAX];
	char *line;
	size_t len;

	fp = fopen(pkgbuild, "r");
	if (!fp) {
		return NULL;
	}

	while (line = fgets(buf, PATH_MAX, fp)) {
		line = strtrim(line);
		len = strlen(line);

		if (!len) {
			continue;
		}

		if (strncmp(line, "depends", 7)) {
			continue;
		}

		/* Parse the array */
		PARSE_BASH_ARRAY_PREAMBLE(line);
		parse_bash_array(&ret, fp, buf, line, 0);
		break;
	}

	fclose(fp);
	return ret;
}

alpm_list_t *resolve_dependencies(struct pw_hashdb *hashdb, alpm_list_t *packages)
{
	alpm_list_t *i, *k, *m, *q;
	alpm_list_t *deps, *newdeps;

	pmpkg_t *pkg;
	struct pkgpair pkgpair;
	struct pkgpair *pkgpair_ptr;
	char pkgbuild[PATH_MAX];
	struct stat st;

	newdeps = NULL;
	for (i = packages; i; i = i->next) {
		snprintf(pkgbuild, PATH_MAX, "%s/PKGBUILD", i->data);
		/* Grab the list of new dependencies from PKGBUILD */
		deps = grab_dependencies(pkgbuild);
		if (!deps) {
			continue;
		}

		if (config->verbose) {
			printf("\nResolving dependencies for %s\n", i->data);
		}

		for (k = deps; k; k = k->next) {
			pkgpair.pkgname = k->data;

			/* Check against newdeps */
			if (alpm_list_find_str(newdeps, k->data)) {
				continue;
			}

			/* Check against localdb */
			if (hash_search(hashdb->local, &pkgpair)) {
				if (config->verbose) {
					printf("%s%s - Already installed\n", TAB, k->data);
				}

				continue;
			}

			/* Check against sync dbs */
			pkgpair_ptr = hash_search(hashdb->sync, &pkgpair);
			if (pkgpair_ptr) {
				if (config->verbose) {
					printf("%s%s can be found in %s repo\n", TAB, k->data,
						   alpm_db_get_name(alpm_pkg_get_db(pkgpair_ptr->pkg)));
				}

				continue;
			}

			/* Check against provides */
			pkgpair_ptr = hashbst_tree_search(hashdb->local_provides, k->data,
											  hashdb->local, provides_search);
			if (pkgpair_ptr) {
				if (config->verbose) {
					printf("%s%s is provided by %s\n", TAB, k->data, pkgpair_ptr->pkgname);
				}
				continue;
			}

			pkgpair_ptr = hashbst_tree_search(hashdb->sync_provides, k->data,
											  hashdb->sync, provides_search);
			if (pkgpair_ptr) {
				if (config->verbose) {
					printf("%s%s is provided by %s\n", TAB, k->data, pkgpair_ptr->pkgname);
				}
				continue;
			}

			/* Check the directory for pkg/PKGBUILD */
			snprintf(pkgbuild, PATH_MAX, "%s/PKGBUILD", k->data);
			if (!stat(pkgbuild, &st)) {
				if (config->verbose) {
					printf("%s%s has been downloaded\n", TAB, k->data);
				}

				continue;
			}

			/* Add to newdeps */
			newdeps = alpm_list_add(newdeps, strdup(k->data));
			if (config->verbose) {
				printf("%s%s will be downloaded from the AUR\n", TAB, k->data);
			}
		}

		/* Free deps */
		FREELIST(deps);
	}

	return newdeps;
}

/* Returns a statically allocated string stating which db the pkg came from
 * @param sdbs sync dbs
 * @param pkgname package to search for
 * @param grp pointer to alpm_list_t * used to store the pkg's groups if any
 */
const char *which_db(alpm_list_t *sdbs, const char *pkgname, alpm_list_t **grp)
{
	const char *repo = NULL;
	alpm_list_t *i, *k;
	pmpkg_t *spkg;

	for (i = sdbs; i && !repo; i = i->next) {
		for (k = alpm_db_get_pkgcache(i->data); k; k = k->next) {
			spkg = k->data;
			if (!strcmp(alpm_pkg_get_name(spkg), pkgname)) {
				repo = alpm_db_get_name(i->data);
				if (grp) {
					*grp = alpm_pkg_get_groups(spkg);
				}

				break;
			}
		}
	}

	if (!repo) {
		repo = LOCAL;
	}

	return repo;
}

/* For plain -Q, -Qs, -Ss */
void print_pkg_pretty(alpm_list_t *sdbs, pmpkg_t *pkg, enum dumplvl_t lvl)
{
	alpm_list_t *grp = NULL;
	const char *repo;
	int found_db, grpcnt;

	repo = which_db(sdbs, alpm_pkg_get_name(pkg), &grp);
	color_repo(repo);
	printf("%s%s %s%s%s", color.bold, alpm_pkg_get_name(pkg),
		   color.bgreen, alpm_pkg_get_version(pkg), color.nocolor);

	color_groups(grp);

	if (lvl == DUMP_Q_SEARCH) {
		printf("%s%s\n", TAB, alpm_pkg_get_desc(pkg));
	} else if (lvl == DUMP_S_SEARCH) {
		printf("%s%s", TAB, alpm_pkg_get_desc(pkg));
	}
}

/* Dumps info from dbs and returns.
 */
int pacman_db_dump(enum pkgfrom_t from, enum dumplvl_t lvl)
{
	int cnt = 0;
	alpm_list_t *i, *j, *dbs, *syncdbs;
	const char *repo;

	pmdb_t *localdb, *db;
	pmpkg_t *pkg;
	pmdepend_t *dep;

	switch (lvl) {
	case DUMP_Q:
	case DUMP_Q_SEARCH:
	case DUMP_Q_INFO:
		localdb = alpm_option_get_localdb();
		syncdbs = alpm_option_get_syncdbs();
		break;
	case DUMP_S_SEARCH:
	case DUMP_S_INFO:
		dbs = alpm_option_get_syncdbs();
		break;
	}

	if (lvl == DUMP_S_SEARCH || lvl == DUMP_S_INFO) {
		goto dump_sync;
	}

	/* -Qi */
	if (lvl == DUMP_Q_INFO) {
		for (i = alpm_db_get_pkgcache(localdb); i; i = i->next) {
			pacman_pkgdump(i->data, PKG_FROM_LOCAL);
		}
	} else {
		/* plain -Q and -Qs */
		for (j = alpm_db_get_pkgcache(localdb); j; j = j->next) {
			pkg = j->data;
			print_pkg_pretty(syncdbs, pkg, lvl);
		}
	}

	goto done;

dump_sync:
	/* -S */
	for (i = dbs; i; i = i->next) {
		db = i->data;

		for (j = alpm_db_get_pkgcache(db); j; j = j->next) {
			if (cnt++) {
				printf("\n");
			}

			pkg = j->data;
			if (lvl == DUMP_S_INFO) {
				pacman_pkgdump(pkg, from);
			} else {
				/* -Ss */
				print_pkg_pretty(dbs, pkg, lvl);
			}
		}
	}

done:
	if (lvl != DUMP_Q && lvl != DUMP_Q_SEARCH) {
		printf("\n");
	}
	return 0;
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

	printf("%s%s%s %-u.%u %c\n", color.bold, prefix, color.nocolor, quo, rem,
		   units[ptr]);
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
		printf("%s%s ", color.bold, REPO);
		const char *repo = alpm_db_get_name(db);
		if (!strcmp(repo, "core")) {
			printf("%s", color.bred);
		} else if (!strcmp(repo, "extra")) {
			printf("%s", color.bgreen);
		} else {
			printf("%s", color.bmag);
		}

		printf("%s%s\n", repo, color.nocolor);
	}

	printf("%s%s%s %s%s%s\n", color.bold, NAME, color.nocolor,
		   color.bold, alpm_pkg_get_name(pkg), color.nocolor);
	printf("%s%s %s%s%s\n", color.bold, VERSION, color.bgreen,
		   alpm_pkg_get_version(pkg), color.nocolor);
	printf("%s%s %s%s%s\n", color.bold, URL, color.bcyan,
		   alpm_pkg_get_url(pkg), color.nocolor);

	print_list_prefix(alpm_pkg_get_licenses(pkg), LICENSES);
	print_list_prefix(alpm_pkg_get_groups(pkg), GROUPS);
	print_list_prefix(alpm_pkg_get_provides(pkg), PROVIDES);

	print_list_deps(alpm_pkg_get_depends(pkg), DEPS);
	print_list_break(alpm_pkg_get_optdepends(pkg), OPTDEPS);

	if (from == PKG_FROM_LOCAL) {
		results = alpm_pkg_compute_requiredby(pkg);
		print_list_prefix(results, REQBY);
	}

	print_list_prefix(alpm_pkg_get_conflicts(pkg), CONFLICTS);
	print_list_prefix(alpm_pkg_get_replaces(pkg), REPLACES);

	if (from == PKG_FROM_SYNC) {
		humanize_size(alpm_pkg_get_size(pkg), DLSZ);
	}

	humanize_size(alpm_pkg_get_isize(pkg), INSTSZ);
	printf("%s%s%s %s\n", color.bold, PKGER, color.nocolor,
		   alpm_pkg_get_packager(pkg));
	printf("%s%s%s %s\n", color.bold, ARCH, color.nocolor, alpm_pkg_get_arch(pkg));
	printf("%s%s%s %s\n", color.bold, BDATE, color.nocolor, builddate);

	if (from == PKG_FROM_LOCAL) {
		printf("%s%s%s %s\n", color.bold, IDATE, color.nocolor, installdate);

		printf("%s%s%s ", color.bold, REASON, color.nocolor);
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
		printf("%s%s%s %s\n", color.bold, SCRIPT, color.nocolor,
			   has_script ? "Yes" : "No");
	}

	if (from == PKG_FROM_SYNC) {
		printf("%s%s%s %s\n", color.bold, MD5SUM, color.nocolor,
			   alpm_pkg_get_md5sum(pkg));
	}

	printf("%s%s%s %s\n", color.bold, DESC, color.nocolor, alpm_pkg_get_desc(pkg));
	FREELIST(results);
}
