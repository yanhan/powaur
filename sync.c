#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <alpm.h>
#include <curl/curl.h>

#include "curl.h"
#include "download.h"
#include "environment.h"
#include "json.h"
#include "package.h"
#include "powaur.h"
#include "sync.h"
#include "util.h"

/* Return a list of struct pkginfo to be freed by caller */
static alpm_list_t *check_installed(alpm_list_t *targets)
{
	alpm_list_t *i, *j, *localdb_cache;
	alpm_list_t *ret = NULL;
	pmdb_t *localdb;
	pmpkg_t *pkg;
	struct pkginfo_t *info;

	localdb = alpm_option_get_localdb();
	ASSERT(localdb != NULL, return NULL);

	localdb_cache = alpm_db_get_pkgcache(localdb);
	for (j = targets; j; j = j->next) {
		for (i = localdb_cache; i; i = i->next) {
			pkg = i->data;
			if (!strcmp(j->data, alpm_pkg_get_name(pkg))) {
				info = pkginfo_new(alpm_pkg_get_name(pkg),
								   alpm_pkg_get_version(pkg),
								   alpm_pkg_get_installdate(pkg));

				ret = alpm_list_add(ret, info);
				break;
			}
		}
	}

	return ret;
}

/* Converts a list of char * (pkg names) to a list of struct pkginfo_t *
 */
static alpm_list_t *char_to_pkginfo(alpm_list_t *list)
{
	alpm_list_t *i, *k, *ret = NULL;
	pmdb_t *localdb;
	pmpkg_t *pkg;
	struct pkginfo_t *pkginfo;

	localdb = alpm_option_get_localdb();
	ASSERT(localdb != NULL, RET_ERR(PW_ERR_LOCALDB_NULL, NULL));

	for (i = list; i; i = i->next) {
		for (k = alpm_db_get_pkgcache(localdb); k; k = k->next) {
			pkg = k->data;
			if (!strcmp(i->data, alpm_pkg_get_name(pkg))) {
				pkginfo = pkginfo_new(i->data, alpm_pkg_get_version(pkg),
									  alpm_pkg_get_installdate(pkg));

				ret = alpm_list_add(ret, pkginfo);
				pkginfo = NULL;
			}
		}
	}

	return ret;
}

/* Saves install results in *success and *failure.
 * Returns a list of alpm_list_t * of struct pkginfo_t * to be freed by caller.
 *
 * This is an extremely ugly function.
 *
 * @param universe list of char * (pkg names)
 * @param org_installed SORTED list of struct pkginfo_t * sorted by name
 * @param nonzero_install SORTED list of char * that may be successfully installed
 * @param success pointer to list that will store successful pkgs
 * @param failure pointer to list that will store failed pkgs
 */
static alpm_list_t *get_install_results(alpm_list_t *universe, alpm_list_t *org_installed,
							   alpm_list_t *nonzero_install,
							   alpm_list_t **success, alpm_list_t **failure)
{
	/* success = (nonzero_install AND not(org_installed)) +
	 * 			 (nonzero_install AND org_installed AND install_date changed)
	 *
	 * failure = universe - success - org_installed
	 */

	alpm_list_t *i, *k;
	alpm_list_t *targets, *newly_installed, *updated_pkg, *tmp_failure;
	alpm_list_t *freelist, *new_universe;

	struct pkginfo_t *pkginfo;
	pmdb_t *localdb;
	pmpkg_t *pkg;

	new_universe = freelist = NULL;
	tmp_failure = targets = newly_installed = updated_pkg = NULL;

	/* ALERT: reload libalpm!!!
	 * This is just a hack to get around us having a not updated localdb
	 * even though packages may have been installed.
	 *
	 * So much just to display what packages have been successfully installed
	 * and stuff. I regret doing this but I wrote too much code and spent too
	 * much time on this to just give everything up.
	 */
	if (alpm_reload()) {
		RET_ERR(PW_ERR_ALPM_RELOAD, NULL);
	}

	/* Convert the important lists to pkginfo structs */
	targets = char_to_pkginfo(nonzero_install);

	new_universe = char_to_pkginfo(universe);
	new_universe = alpm_list_msort(new_universe, alpm_list_count(new_universe),
								   (alpm_list_fn_cmp) pkginfo_name_cmp);

	/* Get those in targets but not in org_installed */
	alpm_list_diff_sorted(targets, org_installed, pkginfo_name_cmp,
						  &newly_installed, NULL);

	pw_printf(PW_LOG_DEBUG, "New Packages installed by us:\n");
	if (!newly_installed) {
		pw_printf(PW_LOG_DEBUG, "None\n");
	} else if (config->loglvl & PW_LOG_DEBUG) {
		for (i = targets; i; i = i->next) {
			pw_printf(PW_LOG_DEBUG, "%s\n", ((struct pkginfo_t *)i->data)->name);
		}

		printf("\n");
	}

	/* Get those in targets and org_installed with install date
	 * modified */
	updated_pkg = list_intersect(targets, org_installed, pkginfo_mod_cmp);

	pw_printf(PW_LOG_DEBUG, "Reinstalled packages / Updated packages:\n");
	if (!updated_pkg) {
		pw_printf(PW_LOG_DEBUG, "None\n");
	} else if (config->loglvl & PW_LOG_DEBUG) {
		for (i = updated_pkg; i; i = i->next) {
			pw_printf(PW_LOG_DEBUG, "Install date modified = %s\n",
					  ((struct pkginfo_t *)i->data)->name);
		}

		printf("\n");
	}

	/* Join the 2 lists */
	if (success) {
		*success = alpm_list_join(newly_installed, updated_pkg);
	}

	/* Find the list of failures.
	 * failure = universe - success - org_installed
	 */
	if (failure) {
		tmp_failure = list_diff(new_universe, *success, pkginfo_name_cmp);
		*failure = list_diff(tmp_failure, org_installed, pkginfo_name_cmp);
	}

	alpm_list_free(tmp_failure);

	freelist = alpm_list_add(freelist, targets);
	freelist = alpm_list_add(freelist, new_universe);
	return freelist;
}

/* Installs a single succesfully downloaded PKGBUILD using makepkg -si
 * returns 0 if installation is successful
 * returns -1 for errors, -2 for fatal errors
 */
static int install_single_package(char *pkgname)
{
	static const char choices[] = {'y', 'n', 'a'};
	int ret;
	char cwd[PATH_MAX];
	char buf[PATH_MAX];
	char *dotinstall;
	pid_t pid;

	snprintf(buf, PATH_MAX, "%s.tar.gz", pkgname);

	ret = extract_file(buf, 0);
	if (ret) {
		RET_ERR(PW_ERR_FILE_EXTRACT, -1);
	}

	if (!getcwd(cwd, PATH_MAX)) {
		RET_ERR(PW_ERR_GETCWD, -1);
	}

	if (chdir(pkgname)) {
		RET_ERR(PW_ERR_CHDIR, -1);
	}

	/* Ask user to edit PKGBUILD */
	snprintf(buf, PATH_MAX, "Edit PKGBUILD for %s? (Y/n/a)", pkgname);
	ret = mcq(buf, choices, sizeof(choices) / sizeof(*choices), 0);

	switch (ret) {
	case 1:
		goto edit_dotinstall;
	case 2:
		/* Abort, propagate upwards */
		return -2;
	}

	/* Fork editor */
	pid = fork();
	if (pid == (pid_t) -1) {
		RET_ERR(PW_ERR_FORK_FAILED, -1);
	} else if (pid == 0) {
		/* Open editor */
		execlp(powaur_editor, powaur_editor, "PKGBUILD", NULL);
	} else {
		ret = wait_or_whine(pid, "vim");
		if (ret) {
			return -1;
		}
	}

edit_dotinstall:
	/* First, check if we have .install file */
	dotinstall = have_dotinstall();
	if (dotinstall) {
		snprintf(buf, PATH_MAX, "Edit .install for %s? (Y/n/a)", pkgname);
		ret = mcq(buf, choices, sizeof(choices) / sizeof(*choices), 0);

		switch (ret) {
		case 1:
			goto fork_pacman;
		case 2:
			free(dotinstall);
			return -2;
		}

		pid = fork();
		if (pid == (pid_t) - 1) {
			RET_ERR(PW_ERR_FORK_FAILED, -1);
		} else if (pid == 0) {
			execlp(powaur_editor, powaur_editor, dotinstall, NULL);
		} else {
			ret = wait_or_whine(pid, "vim");
			if (ret) {
				free(dotinstall);
				return -1;
			}
		}
	}

fork_pacman:
	free(dotinstall);

	if (!yesno("Continue installing %s?", pkgname)) {
		return -2;
	}

	pid = fork();
	if (pid == (pid_t) -1) {
		RET_ERR(PW_ERR_FORK_FAILED, -1);
	} else if (pid == 0) {
		/* Install using pacman */
		execlp("makepkg", "makepkg", "-si", NULL);
	} else {
		/* Parent process */
		ret = wait_or_whine(pid, "makepkg");
		if (ret) {
			return -1;
		}
	}

	/* Change back to old directory */
	if (chdir(cwd)) {
		RET_ERR(PW_ERR_RESTORECWD, -2);
	}

	return 0;
}


/* Installs a list of successfully downloadaded targets using makepkg -si
 * returns -2 on abort, else returns -1 on error, 0 on success
 *
 * Packages that installed successfully are added to success
 */
static int install_packages(alpm_list_t *targets, alpm_list_t **success)
{
	alpm_list_t *i;
	int ret, status;

	ret = status = 0;
	for (i = targets; i; i = i->next) {
		fflush(stdout);
		fprintf(stderr, "\n");
		pw_fprintf(PW_LOG_WARNING, stderr,
				   "Installing unsupported package %s\n         "
				   "You are advised to look through the PKGBUILD.\n", i->data);

		status = install_single_package((char *) i->data);
		if (status == -2) {
			return -2;
		} else if (status == -1) {
			ret = -1;
		} else {
			*success = alpm_list_add(*success, i->data);
		}
	}

	return ret;
}

/* Search sync db for packages. Only works for 1 package now. */
static int sync_search(alpm_list_t *targets)
{
	alpm_list_t *i, *j, *search_results;
	pmdb_t *db;
	pmpkg_t *spkg;
	struct aurpkg_t *pkg;
	size_t listsz;

	search_results = query_aur(targets->data, AUR_QUERY_SEARCH);
	if (search_results == NULL) {
		printf("Sorry, no results for %s\n", targets->data);
		return 0;
	}

	listsz = alpm_list_count(search_results);
	/* Sort by alphabetical order first */
	search_results = alpm_list_msort(search_results, listsz, aurpkg_name_cmp);

	/* Sort by votes */
	if (config->sort_votes) {
		search_results = alpm_list_msort(search_results, listsz, aurpkg_vote_cmp);
	}

	for (i = search_results; i; i = i->next) {
		pkg = (struct aurpkg_t *) i->data;
		printf("aur/%s %s (%d)\n", pkg->name, pkg->version, pkg->votes);
		printf("    %s\n", pkg->desc);
	}

	alpm_list_free_inner(search_results, (alpm_list_fn_free) aurpkg_free);
	alpm_list_free(search_results);

	return 0;
}

/* -Si, search inside sync dbs */
static pmpkg_t *search_syncdbs(alpm_list_t *dbs, const char *pkgname)
{
	alpm_list_t *i, *j;
	pmdb_t *sdb;
	pmpkg_t *spkg;

	for (i = dbs; i; i = i->next) {
		sdb = i->data;

		for (j = alpm_db_get_pkgcache(sdb); j; j = j->next) {
			spkg = j->data;
			if (!strcmp(pkgname, alpm_pkg_get_name(spkg))) {
				return spkg;
			}
		}
	}

	return NULL;
}

/* Lists detailed information about targets */
static int sync_info(alpm_list_t *targets)
{
	int found, ret, pkgcount;
	alpm_list_t *i, *j, *results;
	alpm_list_t *free_list = NULL;
	alpm_list_t *syncdbs = alpm_option_get_syncdbs();
	pmpkg_t *spkg;

	char cwd[PATH_MAX];
	char filename[PATH_MAX];
	char url[PATH_MAX];
	FILE *fp = NULL;
	int fd;
	struct aurpkg_t *pkg;

	if (!getcwd(cwd, PATH_MAX)) {
		RET_ERR(PW_ERR_GETCWD, -1);
	}

	if (chdir(powaur_dir)) {
		RET_ERR(PW_ERR_CHDIR, -1);
	}

	found = ret = pkgcount = 0;
	for (i = targets; i; i = i->next, ++pkgcount) {
		/* Search sync dbs first */
		spkg = search_syncdbs(syncdbs, i->data);
		if (spkg) {
			if (found++){
				printf("\n");
			}

			pacman_pkgdump(spkg, PKG_FROM_SYNC);
			spkg = NULL;
			continue;
		}

		results = query_aur(i->data, AUR_QUERY_INFO);
		if (alpm_list_count(results) != 1) {
			if (pkgcount > 0) {
				printf("\n");
			}

			pw_printf(PW_LOG_ERROR, "package %s not found\n", i->data);
			goto garbage_collect;
		}

		snprintf(filename, PATH_MAX, "%s.PKGBUILDXXXXXX", i->data);
		fd = mkstemp(filename);

		if (fd < 0) {
			PW_SETERRNO(PW_ERR_FOPEN);
			goto garbage_collect;
		}

		fp = fdopen(fd, "w+");
		if (!fp) {
			PW_SETERRNO(PW_ERR_FOPEN);
			goto garbage_collect;
		}

		snprintf(url, PATH_MAX, AUR_PKGBUILD_URL, i->data);

		/* Download the PKGBUILD and parse it */
		ret = download_single_file(url, fp);
		if (ret) {
			goto destroy_remnants;
		}

		/* Parse PKGBUILD and get detailed info */
		fseek(fp, 0L, SEEK_SET);

		pkg = results->data;
		parse_pkgbuild(pkg, fp);

		if (found++) {
			printf("\n");
		}

		printf("%s aur\n", comstrs.i_repo);
		printf("%s %s\n", comstrs.i_name, pkg->name);
		printf("%s %s\n", comstrs.i_version, pkg->version);
		printf("%s %s\n", comstrs.i_url, pkg->url);
		printf("%s ", comstrs.i_aur_url);
		printf(AUR_PKG_URL, pkg->id);
		printf("\n");

		printf("%s %s\n", comstrs.i_licenses, pkg->license);
		printf("%s %d\n", comstrs.i_aur_votes, pkg->votes);
		printf("%s %s\n", comstrs.i_aur_outofdate,
			   pkg->outofdate ? "Yes" : "No");

		print_list(pkg->provides, comstrs.i_provides);
		print_list(pkg->depends, comstrs.i_deps);
		print_list(pkg->optdepends, comstrs.i_optdeps);
		print_list(pkg->conflicts, comstrs.i_conflicts);
		print_list(pkg->replaces, comstrs.i_replaces);
		print_list(pkg->arch, comstrs.i_arch);

		printf("%s %s\n", comstrs.i_desc, pkg->desc);

destroy_remnants:
		fclose(fp);
		fp = NULL;
		unlink(filename);

garbage_collect:
		free_list = alpm_list_add(free_list, results);
	}

cleanup:
	for (i = free_list; i; i = i->next) {
		alpm_list_free_inner(i->data, (alpm_list_fn_free) aurpkg_free);
		alpm_list_free(i->data);
	}

	alpm_list_free(free_list);

	if (chdir(cwd)) {
		RET_ERR(PW_ERR_RESTORECWD, -1);
	}

	return found ? 0 : -1;
}

/* returns 0 upon success.
 * returns -1 upon failure to change dir / download PKGBUILD / install package
 */
int powaur_sync(alpm_list_t *targets)
{
	alpm_list_t *i;
	alpm_list_t *freelist;
	alpm_list_t *org_installed, *dl_failed, *final_targets;
	alpm_list_t *nonzero_install, *success, *failure;
	pmdb_t *localdb;
	int ret, status;

	freelist = NULL;
	nonzero_install = org_installed = dl_failed = final_targets = NULL;
	success = failure = NULL;
	ret = status = 0;

	char orgdir[PATH_MAX];

	/* Makes no sense to run info and search tgt */
	if (config->op_s_search && config->op_s_info) {
		pw_fprintf(PW_LOG_ERROR, stderr,
				   "-s (search) and -i (info) are mutually exclusive\n");
		return -1;
	}

	if (targets == NULL) {
		if (config->op_s_search) {
			ret = pacman_db_info(alpm_option_get_syncdbs(), PKG_FROM_SYNC, 1);
		} else if (config->op_s_info) {
			ret = pacman_db_info(alpm_option_get_syncdbs(), PKG_FROM_SYNC, 0);
		} else {
			pw_fprintf(PW_LOG_ERROR, stderr, "No targets specified for sync\n");
			ret = -1;
		}

		return ret;
	}

	if (config->op_s_search) {
		/* Search for packages on AUR */
		ret = sync_search(targets);
		return ret;
	} else if (config->op_s_info) {
		ret = sync_info(targets);
		return ret;
	}

	/* Save our current directory */
	if (!getcwd(orgdir, PATH_MAX)) {
		RET_ERR(PW_ERR_GETCWD, -1);
	}

	if (ret = chdir(powaur_dir)) {
		PW_SETERRNO(PW_ERR_CHDIR);
		goto cleanup;
	}

	alpm_list_t *iptr;
	struct pkginfo_t *pkginfo;
	org_installed = check_installed(targets);
	org_installed = alpm_list_msort(org_installed, alpm_list_count(org_installed),
									pkginfo_name_cmp);

	/* Check */
	for (iptr = org_installed; iptr; iptr = iptr->next) {
		pkginfo = iptr->data;
		pw_printf(PW_LOG_DEBUG, "Originally installed: %s %s\n",
				  pkginfo->name, pkginfo->version);
	}

	/* DL the packages, get the working ones and install them */
	ret = download_packages(targets, &dl_failed);
	final_targets = list_diff(targets, dl_failed, (alpm_list_fn_cmp) strcmp);

	ret = install_packages(final_targets, &nonzero_install);

	nonzero_install = alpm_list_msort(nonzero_install,
									  alpm_list_count(nonzero_install),
									  (alpm_list_fn_cmp) strcmp);

	freelist = get_install_results(targets, org_installed, nonzero_install,
						&success, &failure);

	if (org_installed) {
		printf("\n");
		pw_printf(PW_LOG_INFO, "These packages were originally installed:\n");
		print_pkginfo(org_installed);
	}

	if (success) {
		printf("\n");
		pw_printf(PW_LOG_INFO,
				  "The following packages were successfully installed\n");
		print_pkginfo(success);
	}

	if (failure) {
		printf("\n");
		pw_printf(PW_LOG_WARNING, "The following packages were not installed:\n");
		print_pkginfo(failure);
	}

cleanup:
	if (chdir(orgdir)) {
		PW_SETERRNO(PW_ERR_RESTORECWD);
	}

	alpm_list_free_inner(org_installed, (alpm_list_fn_free) pkginfo_free_all);
	alpm_list_free(org_installed);

	alpm_list_free(dl_failed);
	alpm_list_free(final_targets);
	alpm_list_free(nonzero_install);

	for (i = freelist; i; i = i->next) {
		alpm_list_free_inner(i->data, (alpm_list_fn_free) pkginfo_free_all);
		alpm_list_free(i->data);
	}

	alpm_list_free(freelist);
	alpm_list_free(success);
	alpm_list_free(failure);

	return ret ? -1 : 0;
}

/* -M (--maintainer) search. */
int powaur_maint(alpm_list_t *targets)
{
	if (!targets) {
		pw_printf(PW_LOG_ERROR, "argument needed for -M\n");
		return -1;
	} else if (alpm_list_count(targets) > 1) {
		pw_printf(PW_LOG_ERROR, "-M only takes 1 argument\n");
		return -1;
	}

	alpm_list_t *i, *results;
	struct aurpkg_t *pkg;
	size_t listsz;

	/* Clear pwerrno */
	CLEAR_ERRNO();
	results = query_aur(targets->data, AUR_QUERY_MSEARCH);

	if (pwerrno != PW_ERR_OK) {
		return -1;
	} else if (!results) {
		printf("No packages found.\n");
		return 0;
	}

	/* Sort by alphabetical order */
	listsz = alpm_list_count(results);
	results = alpm_list_msort(results, listsz, aurpkg_name_cmp);

	if (config->sort_votes) {
		results = alpm_list_msort(results, listsz, aurpkg_vote_cmp);
	}

	for (i = results; i; i = i->next) {
		pkg = i->data;
		printf("aur/%s (%d)\n%s%s\n", pkg->name, pkg->votes, comstrs.tab,
			   pkg->desc);
	}

	alpm_list_free_inner(results, (alpm_list_fn_free) aurpkg_free);
	alpm_list_free(results);
	return 0;
}
