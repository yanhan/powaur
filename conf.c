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
	conf->color = 1;

	return conf;
}

void config_free(struct config_t *conf)
{
	if (conf) {
		free(conf->target_dir);
		free(conf);
	}
}

/* fp is assumed to be valid.
 */
void parse_powaur_config(FILE *fp)
{
	char buf[PATH_MAX];
	char *line, *key, *val;
	size_t len;

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
				free(powaur_editor);
			}

			powaur_editor = xstrdup(val);
			pw_printf(PW_LOG_DEBUG, "%s%sParsed Editor = %s\n", TAB, TAB,
					  powaur_editor);

		} else if (!strcmp(key, "TmpDir")) {
			if (powaur_dir) {
				free(powaur_dir);
			}

			powaur_dir = xstrdup(val);
			pw_printf(PW_LOG_DEBUG, "%s%sParsed TmpDir = %s\n", TAB, TAB,
					  powaur_dir);

		} else if (!strcmp(key, "MaxThreads")) {
			if (config->opt_maxthreads) {
				pw_printf(PW_LOG_DEBUG, "%s%s--threads = %d, overriding config\n",
						  TAB, TAB, powaur_maxthreads);
				continue;
			}

			powaur_maxthreads = atoi(val);
			pw_printf(PW_LOG_DEBUG, "%s%sParsed MaxThreads = %d\n", TAB, TAB,
					  powaur_maxthreads);

			if (powaur_maxthreads < 1 || powaur_maxthreads > PW_DEF_MAXTHREADS) {
				powaur_maxthreads = 0;
			}

		} else if (!strcmp(key, "Color")) {
			if (!strcmp(val, "Off") && config->color > 0) {
				--config->color;
				pw_printf(PW_LOG_DEBUG, "%s%sParsed Color = Off\n", TAB, TAB);
			} else if (!strcmp(val, "On")) {
				++config->color;
				pw_printf(PW_LOG_DEBUG, "%s%sParsed Color = On\n", TAB, TAB);
			}
		} else if (!strcmp(key, "NoConfirm")) {
			if (!strcmp(val, "Off") && config->noconfirm > 0) {
				config->noconfirm = 0;
				pw_printf(PW_LOG_DEBUG, "%s%sParsed NoConfirm = Off\n", TAB, TAB);
			} else if (!strcmp(val, "On")) {
				config->noconfirm = 1;
				pw_printf(PW_LOG_DEBUG, "%s%sParsed NoConfirm = On\n", TAB, TAB);
			}
		}
	}
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
	pw_printf(PW_LOG_DEBUG, "%s%sDefault %s = %s\n", TAB, TAB, key, *env_conf);

	if (comment) {
		*flag_com = 1;
	} else {
		*flag = 1;
	}
}

/* A lot of hell can break loose.
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

		if (!strncmp(line, ROOTDIR, 7)) {
			parse_pmoption_keyval(&rootdir, &rootdir_com, line, ROOTDIR,
								  &pacman_rootdir, 1);

		} else if (!strncmp(line, DBPATH, 6)) {
			parse_pmoption_keyval(&dbpath, &dbpath_com, line, DBPATH,
								  &pacman_dbpath, 1);

		} else if (!strncmp(line, CACHEDIR, 8)) {
			parse_pmoption_repeat(&cachedir, &cachedir_com, line, CACHEDIR,
								  setrepeat_cachedir, free_cachedir,
								  (void **) &pacman_cachedirs, 1);

			pw_printf(PW_LOG_DEBUG, "%s%sCachedir default:\n", TAB, TAB);
			indent_print(PW_LOG_DEBUG, pacman_cachedirs, 12);
		}

	} else {
		/* Non-commented, ie. official stuff */

		if (!strncmp(line, ROOTDIR, 7)) {
			if (rootdir) {
				pw_printf(PW_LOG_ERROR, "%s%sRepeated %s\n", TAB, TAB, ROOTDIR);
				return -1;
			}

			parse_pmoption_keyval(&rootdir, &rootdir_com, line, ROOTDIR,
								  &pacman_rootdir, 0);

		} else if (!strncmp(line, DBPATH, 6)) {
			if (dbpath) {
				pw_printf(PW_LOG_ERROR, "%s%sRepeated %s\n", TAB, TAB, DBPATH);
				return -1;
			}

			parse_pmoption_keyval(&dbpath, &dbpath_com, line, DBPATH,
								  &pacman_dbpath, 0);

		} else if (!strncmp(line, CACHEDIR, 8)) {
			if (cachedir) {
				pw_printf(PW_LOG_ERROR, "%s%sRepeated %s\n", TAB, TAB, CACHEDIR);
				return -1;
			}

			parse_pmoption_repeat(&cachedir, &cachedir_com, line, CACHEDIR,
								  setrepeat_cachedir, free_cachedir,
								  (void **) &pacman_cachedirs, 0);

			pw_printf(PW_LOG_DEBUG, "%s%sCachedir final:\n", TAB, TAB);
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

	fp = fopen(PMCONF, "r");
	if (!fp) {
		return error(PW_ERR_PM_CONF_OPEN);
	}

	pw_printf(PW_LOG_DEBUG, "%s : Parsing %s\n", __func__, PMCONF);

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

			if (!strcmp(line, OPT)) {
				if (parsed_options) {
					pw_printf(PW_LOG_ERROR, "%sRepeated %s section in %s\n",
							  TAB, OPT, PMCONF);
					ret = -1;
					goto cleanup;
				}

				pw_printf(PW_LOG_DEBUG, "%sParsing [%s] section of %s\n",
						  TAB, OPT, PMCONF);

				parsed_options = 1;
				in_options = 1;
				continue;

			} else if (len == 0) {
				pw_printf(PW_LOG_DEBUG, "%sEmpty section in %s\n", TAB, PMCONF);
				ret = -1;
				goto cleanup;
			} else {
				/* Must be a repository
				 * We just add the repository for now
				 */
				in_options = 0;

				pw_printf(PW_LOG_DEBUG, "%sParsing Repo [%s]\n", TAB, line);

				if (!alpm_db_register_sync(line)) {
					pw_printf(PW_LOG_ERROR, "%sFailed to register %s db\n",
							  TAB, line);
					goto cleanup;
				}

				pw_printf(PW_LOG_DEBUG, "%sRegistering sync database '%s'\n",
						  TAB, line);
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
