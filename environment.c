#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

#include <alpm_list.h>

#include "conf.h"
#include "environment.h"
#include "util.h"
#include "wrapper.h"

struct config_t *config;

enum _pw_errno_t pwerrno = PW_ERR_OK;
char *powaur_dir;
char *powaur_editor;
int powaur_maxthreads;

struct colorstrs color;

/* Pacman configuration */
char *pacman_rootdir;
char *pacman_dbpath;
alpm_list_t *pacman_cachedirs;

int setup_config(void)
{
	config = config_init();
	if (!config) {
		return error(PW_ERR_INIT_CONFIG);
	}

	return 0;
}

/* Initialize colors */
void colors_setup(void)
{
	if (config->color > 0) {
		color.black   = xstrdup(BLACK);
		color.red     = xstrdup(RED);
		color.green   = xstrdup(GREEN);
		color.yellow  = xstrdup(YELLOW);
		color.blue    = xstrdup(BLUE);
		color.mag     = xstrdup(MAG);
		color.cyan    = xstrdup(CYAN);
		color.white   = xstrdup(WHITE);
		color.bblack  = xstrdup(BBLACK);
		color.bred    = xstrdup(BRED);
		color.bgreen  = xstrdup(BGREEN);
		color.byellow = xstrdup(BYELLOW);
		color.bblue   = xstrdup(BBLUE);
		color.bmag    = xstrdup(BMAG);
		color.bcyan   = xstrdup(BCYAN);
		color.bwhite  = xstrdup(BWHITE);
		color.nocolor = xstrdup(NOCOLOR);
		color.bold    = xstrdup(BOLD);
		color.votecol = xstrdup(VOTECOL);

		/* Setup colorized print functions */
		color_print_setup();

	} else {
		color.black   = xstrdup("");
		color.red     = xstrdup("");
		color.green   = xstrdup("");
		color.yellow  = xstrdup("");
		color.blue    = xstrdup("");
		color.mag     = xstrdup("");
		color.cyan    = xstrdup("");
		color.white   = xstrdup("");
		color.bblack  = xstrdup("");
		color.bred    = xstrdup("");
		color.bgreen  = xstrdup("");
		color.byellow = xstrdup("");
		color.bblue   = xstrdup("");
		color.bmag    = xstrdup("");
		color.bcyan   = xstrdup("");
		color.bwhite  = xstrdup("");
		color.nocolor = xstrdup("");
		color.bold    = xstrdup("");
		color.votecol = xstrdup("");

		/* Restore non-colorized print functions */
		color_print_restore();
	}
}

static void colors_cleanup(void)
{
	free(color.black);
	free(color.red);
	free(color.green);
	free(color.yellow);
	free(color.blue);
	free(color.mag);
	free(color.cyan);
	free(color.white);
	free(color.bblack);
	free(color.bred);
	free(color.bgreen);
	free(color.byellow);
	free(color.bblue);
	free(color.bmag);
	free(color.bcyan);
	free(color.bwhite);
	free(color.nocolor);
	free(color.bold);
	free(color.votecol);
}

/* @param reload set to > 0 if reloading libalpm so that cachedirs can be
 * reinitialized.
 */
static int setup_pacman_environment(int reload)
{
	if (reload) {
		pw_printf(PW_LOG_DEBUG, "Reloading pacman configuration\n");
		pacman_cachedirs = NULL;
	}

	if (parse_pmconfig()) {
		/* Free cachedirs */
		FREELIST(pacman_cachedirs);
		return error(PW_ERR_PM_CONF_PARSE);
	}

	if (!pacman_rootdir) {
		pacman_rootdir = xstrdup(PACMAN_DEF_ROOTDIR);
	}

	if (!pacman_dbpath) {
		pacman_dbpath = xstrdup(PACMAN_DEF_DBPATH);
	}

	if (!pacman_cachedirs) {
		pacman_cachedirs = alpm_list_add(pacman_cachedirs,
										 xstrdup(PACMAN_DEF_CACHEDIR));
	}

	alpm_option_set_root(pacman_rootdir);
	alpm_option_set_dbpath(pacman_dbpath);
	alpm_option_set_cachedirs(pacman_cachedirs);

	return 0;
}

static int setup_powaur_config(void)
{
	/* Check the following places for logfile:
	 * $XDG_CONFIG_HOME
	 * $HOME
	 *
	 * Then fallback to default settings
	 */

	int ret = -1;
	FILE *fp = NULL;
	char *dir;
	char buf[PATH_MAX];
	struct stat st;

	pw_printf(PW_LOG_DEBUG, "%s: Setting up powaur configuration\n", __func__);

	dir = getenv("XDG_CONFIG_HOME");
	if (dir) {
		snprintf(buf, PATH_MAX, "%s/%s", dir, PW_CONF);

		if (!stat(buf, &st)) {
			fp = fopen(buf, "r");
			if (!fp) {
				goto check_home;
			}

			pw_printf(PW_LOG_DEBUG, "%sParsing %s\n", TAB, buf);
			parse_powaur_config(fp);
			fclose(fp);
			goto cleanup;
		}
	}

check_home:
	/* Check $HOME */
	dir = getenv("HOME");
	if (dir) {
		snprintf(buf, PATH_MAX, "%s/.config/%s", dir, PW_CONF);

		if (!stat(buf, &st)) {
			fp = fopen(buf, "r");
			if (!fp) {
				goto cleanup;
			}

			pw_printf(PW_LOG_DEBUG, "%sParsing %s\n", TAB, buf);
			(fp);
			fclose(fp);
		}
	}

cleanup:

	/* Use default settings for unspecified options */
	if (!powaur_dir) {
		uid_t myuid = geteuid();
		struct passwd *passwd_struct = getpwuid(myuid);
		if (!passwd_struct) {
			powaur_dir = xstrdup(PW_DEF_DIR);
		} else {
			snprintf(buf, PATH_MAX, "%s-%s", PW_DEF_DIR, passwd_struct->pw_name);
			powaur_dir = xstrdup(buf);
		}

		pw_printf(PW_LOG_DEBUG, "%sFalling back to default directory %s\n",
				  TAB, powaur_dir);
	}

	if (!powaur_editor) {
		powaur_editor = xstrdup(PW_DEF_EDITOR);
		pw_printf(PW_LOG_DEBUG, "%sFalling back to default editor %s\n",
				  TAB, powaur_editor);
	}

	if (powaur_maxthreads <= 0 || powaur_maxthreads > PW_DEF_MAXTHREADS) {
		powaur_maxthreads = PW_DEF_MAXTHREADS;
	}

	config->maxthreads = powaur_maxthreads;
	pw_printf(PW_LOG_DEBUG, "%sMaximum no. of threads = %d\n", TAB,
			  config->maxthreads);

	return 0;
}

int setup_environment(void)
{
	if (setup_pacman_environment(0)) {
		return -1;
	}

	if (setup_powaur_config()) {
		return -1;
	}

	return 0;
}

void cleanup_environment(void)
{
	if (config) {
		if (config->clean_tempdir)
			rmrf(powaur_dir);
		config_free(config);
	}

	colors_cleanup();
	free(powaur_editor);
	free(powaur_dir);

	/* No need to free pacman_cachedirs */
	free(pacman_rootdir);
	free(pacman_dbpath);
}
