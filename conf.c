#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <alpm.h>
#include <alpm_list.h>

#include "conf.h"
#include "environment.h"
#include "powaur.h"
#include "util.h"
#include "wrapper.h"

struct config_t *config_init(void)
{
	struct config_t *conf;
	conf = xcalloc(1, sizeof(struct config_t));
	conf->op = PW_OP_MAIN;
	conf->loglvl = PW_LOG_NORM | PW_LOG_INFO | PW_LOG_WARNING | PW_LOG_ERROR;

	return conf;
}

void config_free(struct config_t *conf)
{
	if (conf) {
		free(conf->target_dir);
		free(conf);
	}
}

/* fp is guaranteed to be non-NULL.
 * returns 0 on success, -1 on failure, 1 if not all options specified
 */
int parse_powaur_config(FILE *fp)
{
	int ret, parsed;
	char buf[PATH_MAX];
	char *line, *key, *val;
	size_t len;

	ret = parsed = 0;

	while (line = fgets(buf, PATH_MAX, fp)) {
		line = strtrim(line);
		len = strlen(line);

		/* Ignore empty lines and comments */
		if (!len || line[0] == '#') {
			continue;
		}

		val = strchr(line, '=');
		if (!val) {
			continue;
		}

		*val = 0;
		++val;

		key = strtrim(line);
		val = strtrim(val);

		if (!strcmp(key, "Editor")) {
			if (powaur_editor) {
				pw_fprintf(PW_LOG_ERROR, stderr, "%s%sRepeated Editor option!\n",
						   comstrs.tab, comstrs.tab);
				ret = -1;
				goto cleanup;
			}

			powaur_editor = strdup(val);
			if (!powaur_editor) {
				ret = -1;
				goto cleanup;
			}

			++parsed;
			pw_printf(PW_LOG_DEBUG, "%s%sParsed Editor = %s\n", comstrs.tab,
					  comstrs.tab, powaur_editor);

		} else if (!strcmp(key, "TmpDir")) {
			if (powaur_dir) {
				pw_fprintf(PW_LOG_ERROR, stderr, "%s%sRepeated TmpDir option!\n",
						   comstrs.tab, comstrs.tab);
				ret = -1;
				goto cleanup;
			}

			powaur_dir = strdup(val);
			if (!powaur_dir) {
				ret = -1;
				goto cleanup;
			}

			++parsed;
			pw_printf(PW_LOG_DEBUG, "%s%sParsed TmpDir = %s\n", comstrs.tab,
					  comstrs.tab, powaur_dir);
		}
	}

cleanup:

	if (ret) {
		if (!parsed) {
			free(powaur_editor);
			free(powaur_dir);
			powaur_editor = powaur_dir = NULL;
			return -1;
		} else {
			return 1;
		}
	}

	return 0;
}

static void parse_pmoption_keyval(int *flag, int *flag_com, char *line,
								  const char *key, char **env_conf, int comment)
{
	static const char *keyval_sep = "=";

	if ((comment && (*flag || *flag_com)) || (!comment && *flag)) {
		return;
	}

	strsep(&line, keyval_sep);
	line = strtrim(line);

	if (!strlen(line)) {
		return;
	}

	/* Set global */
	if (*env_conf) {
		free(*env_conf);
	}

	*env_conf = strdup(line);
	pw_printf(PW_LOG_DEBUG, "%s%sDefault %s = %s\n",
			  comstrs.tab, comstrs.tab, key, *env_conf);

	if (comment) {
		*flag_com = 1;
	} else {
		*flag = 1;
	}
}

/* TODO: Get rid of this parsing comments shit quick.
 * A lot of hell can break loose.
 */
static void free_cachedir(void **cachedir)
{
	FREELIST(*(alpm_list_t **) cachedir);
}

static void setrepeat_cachedir(char *dir)
{
	pacman_cachedirs = alpm_list_add(pacman_cachedirs, strdup(dir));
}

/* Disgusting 8 variable function */
static void parse_pmoption_repeat(int *flag, int *flag_com, char *line,
								  const char *key, void (*repeatfn) (char *),
								  void (*freefn) (void **), void **global_conf,
								  int comment)
{
	if ((comment && (*flag || *flag_com)) || (!comment && *flag)) {
		return;
	}

	if (!(line = strchr(line, '='))) {
		return;
	}

	if (*global_conf) {
		freefn(global_conf);
		*global_conf = NULL;
	}

	++line;
	line = strtrim(line);
	if (!strlen(line)) {
		return;
	}

	char *ptr;
	while (ptr = strchr(line, ' ')) {
		*ptr = 0;
		++ptr;

		repeatfn(line);
		line = ptr;
		line = strtrim(line);
	}

	repeatfn(line);

	if (comment) {
		*flag_com = 1;
	} else {
		*flag = 1;
	}
}

/* Line is assumed to have been strtrim'ed
 */
static int _parse_pmoption(char *line)
{
	static int rootdir_com, rootdir;
	static int dbpath_com, dbpath;
	static int cachedir_com, cachedir;

	int len;

	if (line[0] == '#') {

		/* Look for default settings first. */
		++line;
		line = strtrim(line);
		len = strlen(line);

		if (len == 0) {
			return 0;
		}

		if (!strncmp(line, comstrs.rootdir, 7)) {
			parse_pmoption_keyval(&rootdir, &rootdir_com, line, comstrs.rootdir,
								  &pacman_rootdir, 1);

		} else if (!strncmp(line, comstrs.dbpath, 6)) {
			parse_pmoption_keyval(&dbpath, &dbpath_com, line, comstrs.dbpath,
								  &pacman_dbpath, 1);

		} else if (!strncmp(line, comstrs.cachedir, 8)) {
			parse_pmoption_repeat(&cachedir, &cachedir_com, line,
								  comstrs.cachedir, setrepeat_cachedir,
								  free_cachedir, (void **) &pacman_cachedirs, 1);

			pw_printf(PW_LOG_DEBUG, "%s%sCachedir default:\n", comstrs.tab,
					  comstrs.tab);

			indent_print(PW_LOG_DEBUG, pacman_cachedirs, 12);
		}

	} else {
		/* Non-commented, ie. official stuff */

		if (!strncmp(line, comstrs.rootdir, 7)) {
			if (rootdir) {
				pw_printf(PW_LOG_ERROR, "%s%sRepeated %s\n",
						  comstrs.tab, comstrs.tab, comstrs.rootdir);
				return -1;
			}

			parse_pmoption_keyval(&rootdir, &rootdir_com, line, comstrs.rootdir,
								  &pacman_rootdir, 0);

		} else if (!strncmp(line, comstrs.dbpath, 6)) {
			if (dbpath) {
				pw_printf(PW_LOG_ERROR, "%s%sRepeated %s\n",
						  comstrs.tab, comstrs.tab, comstrs.dbpath);
				return -1;
			}

			parse_pmoption_keyval(&dbpath, &dbpath_com, line, comstrs.dbpath,
								  &pacman_dbpath, 0);

		} else if (!strncmp(line, comstrs.cachedir, 8)) {
			if (cachedir) {
				pw_printf(PW_LOG_ERROR, "%s%sRepeated %s\n", comstrs.tab,
						  comstrs.tab, comstrs.cachedir);
				return -1;
			}

			parse_pmoption_repeat(&cachedir, &cachedir_com, line,
								  comstrs.cachedir, setrepeat_cachedir,
								  free_cachedir, (void **) &pacman_cachedirs, 0);

			pw_printf(PW_LOG_DEBUG, "%s%sCachedir final:\n", comstrs.tab,
					  comstrs.tab);
			indent_print(PW_LOG_DEBUG, pacman_cachedirs, 12);
		}
	}

	return 0;
}

/* Parse /etc/pacman.conf
 * We are hoping that the user does not remove the commented lines
 * under the [options] section.
 *
 * If there is insufficient information, we will fallback to the defaults.
 */
int parse_pmconfig(void)
{
	int ret = 0;
	int len;

	FILE *fp;
	char buf[PATH_MAX];
	char *line;

	int in_options = 0;
	int parsed_options = 0;

	fp = fopen(comstrs.pmconf, "r");
	if (!fp) {
		return error(PW_ERR_PM_CONF_OPEN);
	}

	pw_printf(PW_LOG_DEBUG, "%s : Parsing %s\n", __func__, comstrs.pmconf);

	while (line = fgets(buf, PATH_MAX, fp)) {
		line = strtrim(line);
		len = strlen(line);

		/* Ignore empty lines / Comments */
		if (len == 0 || (!in_options && line[0] == '#')) {
			continue;
		}

		/* Entering a section */
		if (line[0] == '[' && line[len-1] == ']') {

			line[len-1] = 0;
			++line;
			line = strtrim(line);

			/* Get new length of line */
			len = strlen(line);

			if (!strcmp(line, comstrs.opt)) {
				if (parsed_options) {
					pw_printf(PW_LOG_ERROR, "%sRepeated %s section in %s\n",
							  comstrs.tab, comstrs.opt, comstrs.pmconf);
					ret = -1;
					goto cleanup;
				}

				pw_printf(PW_LOG_DEBUG, "%sParsing [%s] section of %s\n",
						  comstrs.tab, comstrs.opt, comstrs.pmconf);

				parsed_options = 1;
				in_options = 1;
				continue;

			} else if (len == 0) {
				pw_printf(PW_LOG_DEBUG, "%sEmpty section in %s\n",
						  comstrs.tab, comstrs.pmconf);
				ret = -1;
				goto cleanup;
			} else {
				/* Must be a repository
				 * We just add the repository for now
				 */
				in_options = 0;

				pw_printf(PW_LOG_DEBUG, "%sParsing Repo [%s]\n",
						  comstrs.tab, line);

				if (!alpm_db_register_sync(line)) {
					pw_printf(PW_LOG_ERROR, "%sFailed to register %s db\n",
							  comstrs.tab, line);
					goto cleanup;
				}

				pw_printf(PW_LOG_DEBUG, "%sRegistering sync database '%s'\n",
						  comstrs.tab, line);
			}

		} else if (in_options) {
			if (ret = _parse_pmoption(line)) {
				break;
			}

			continue;
		}
	}

cleanup:
	fclose(fp);
	return ret;
}
