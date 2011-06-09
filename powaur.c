#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <getopt.h>

#include <alpm.h>
#include <curl/curl.h>
#include <yajl/yajl_parse.h>

#include "curl.h"
#include "download.h"
#include "environment.h"
#include "handle.h"
#include "json.h"
#include "package.h"
#include "powaur.h"
#include "sync.h"
#include "util.h"

static alpm_list_t *powaur_targets = NULL;

static int powaur_cleanup(int ret)
{
	FREELIST(powaur_targets);
	curl_cleanup();
	_pwhandle_free(pwhandle);
	cleanup_environment();
	alpm_release();

	exit(ret);
}

static int powaur_init(void)
{
	struct stat st;
	int ret = 0;

	if (alpm_initialize() == -1) {
		return -1;
	}

	if (setup_environment()) {
		return error(PW_ERR_INIT_ENV);
	}

	pwhandle = _pwhandle_init();
	if (!pwhandle) {
		return error(PW_ERR_INIT_HANDLE);
	}

	if (stat(powaur_dir, &st) != 0) {
		if (mkdir(powaur_dir, 0755)) {
			return error(PW_ERR_INIT_DIR);
		}
	}

	if (curl_init()) {
		return error(PW_ERR_CURL_INIT);
	}

	return ret;
}

static void usage(unsigned short op)
{
	if (op == PW_OP_MAIN) {
		printf("%s %s <operation> [...]\n", USAGE, MYNAME);
		printf("%s%s {-h --help}\n", TAB, MYNAME);
		printf("%s%s {-G --getpkgbuild} <%s>\n", TAB, MYNAME, PKG);
		printf("%s%s --crawl <%s>\n", TAB, MYNAME, PKG);
		printf("%s%s {-S --sync}        [%s] [%s]\n", TAB, MYNAME, OPT, PKG);
		printf("%s%s {-Q --query}       [%s] [%s]\n", TAB, MYNAME, OPT, PKG);
		printf("%s%s {-M --maintainer}  <%s>\n", TAB, MYNAME, PKG);
		printf("%s%s {-B --backup} [dir]\n", TAB, MYNAME);
		printf("%s%s {-V --version}\n", TAB, MYNAME);
	} else {
		if (op == PW_OP_SYNC) {
			printf("%s %s {-S --sync} [%s] [%s]\n", USAGE, MYNAME, OPT, PKG);
		} else if (op == PW_OP_QUERY) {
			printf("%s %s {-Q --query} [%s] [%s]\n", USAGE, MYNAME, OPT, PKG);
		} else if (op == PW_OP_GET) {
			printf("%s %s {-G --getpkgbuild <%s>\n", USAGE, MYNAME, PKG);
		} else if (op == PW_OP_MAINTAINER) {
			printf("%s %s {-M --maintainer <%s>\n", USAGE, MYNAME, PKG);
		} else if (op == PW_OP_BACKUP) {
			printf("%s %s {-B --backup} [dir]\n", USAGE, MYNAME);
		}

		printf("%s:\n", OPT);

		switch (op) {
		case PW_OP_SYNC:
		case PW_OP_QUERY:
			printf("  -i, --info                 view package information\n");
			printf("  -s, --search <%s>  search %s repositories for %s\n",
				   PKG, op == PW_OP_SYNC ? "sync" : "local", PKG);
			break;
		case PW_OP_GET:
			printf("      --target <DIR>         downloads to alternate directory DIR\n");
			printf("      --deps                 resolves dependencies\n");
			printf("      --threads <N>          limit max no. of threads to N\n");
			break;
		case PW_OP_MAINTAINER:
			printf("      --vote                 order search results by votes\n");
			break;
		case PW_OP_CRAWL:
			printf("      --crawl <%s>    outputs dependency graph for %s\n", PKG, PKG);
			break;
		default:
			break;
		}

		if (op == PW_OP_SYNC) {
			printf("      --check                Works with -u, checks for outdated packages without upgrading\n");
			printf("      --vote                 order search results by votes\n");
		}

		printf("      --debug                display debug messages\n");
		printf("      --verbose              display more messages\n");
		printf("      --color                Switches on color\n");
		printf("      --no-color             Switches off color\n");
	}

cleanup:
	powaur_cleanup(1);
}

static void version()
{
	printf("powaur %s\n", POWAUR_VERSION);
	powaur_cleanup(0);
}

/* From pacman */
static int parsearg_op(int option, int dry_run)
{
	switch (option) {
	case 'G':
		if (dry_run) break;
		config->op = (config->op == PW_OP_MAIN ? PW_OP_GET : PW_OP_INVAL);
		break;
	case 'S':
		if (dry_run) break;
		config->op = (config->op == PW_OP_MAIN ? PW_OP_SYNC : PW_OP_INVAL);
		break;
	case 'Q':
		if (dry_run) break;
		config->op = (config->op == PW_OP_MAIN ? PW_OP_QUERY : PW_OP_INVAL);
		break;
	case 'M':
		if (dry_run) break;
		config->op = (config->op == PW_OP_MAIN ? PW_OP_MAINTAINER : PW_OP_INVAL);
		break;
	case 'B':
		if (dry_run) break;
		config->op = (config->op == PW_OP_MAIN ? PW_OP_BACKUP : PW_OP_INVAL);
		break;
	case PW_OP_CRAWL:
		if (dry_run) break;
		config->op = (config->op == PW_OP_MAIN ? PW_OP_CRAWL : PW_OP_INVAL);
		break;
	case 'h':
		if (dry_run) break;
		config->help = 1;
		break;
	case 'V':
		if (dry_run) break;
		config->version = 1;
		break;
	default:
		return -1;
	}

	return 0;
}

/* Parse options for -S (--sync) */
static int parsearg_sync(int option)
{
	switch (option) {
	case 'i':
		config->op_s_info = 1;
		break;
	case 's':
		config->op_s_search = 1;
		break;
	case 'u':
		config->op_s_upgrade = 1;
		break;
	default:
		return -1;
	}

	return 0;
}

/* Parse options for -Q (--query) */
static int parsearg_query(int option)
{
	switch (option) {
	case 'i':
		config->op_q_info = 1;
		break;
	case 's':
		config->op_q_search = 1;
		break;
	default:
		return -1;
	}

	return 0;
}

/* Parse options for -G (--getpkgbuild) */
static int parsearg_get(int option)
{
	switch (option) {
	case OPT_RESOLVE_DEPS:
		config->op_g_resolve = 1;
		break;
	default:
		return -1;
	}

	return 0;
}

/* Parse global arguments */
static int parsearg_global(int option)
{
	switch (option) {
	case OPT_DEBUG:
		config->loglvl |= PW_LOG_DEBUG;
		break;
	case OPT_SORT_VOTE:
		config->sort_votes = 1;
		break;
	case OPT_VERBOSE:
		config->verbose = 1;
		break;
	case OPT_TARGET_DIR:
		config->target_dir = strdup(optarg);
		break;
	case OPT_MAXTHREADS:
		config->opt_maxthreads = 1;
		powaur_maxthreads = atoi(optarg);
		break;
	case OPT_COLOR:
		if (!config->color_set) {
			++config->color;
			config->color_set = 1;
		}
		break;
	case OPT_NOCOLOR:
		if (!config->nocolor_set) {
			--config->color;
			config->nocolor_set = 1;
		}
		break;
	default:
		return -1;
	}

	return 0;
}


/* Borrowed from pacman */
static int parseargs(int argc, char *argv[])
{
	int opt, option_index = 0;
	int res;

	const char *optstring = "BGMQSVhisuw";

	static struct option opts[] = {
		{"backup", no_argument, NULL, 'B'},
		{"getpkgbuild", no_argument, NULL, 'G'},
		{"maintainer", no_argument, NULL, 'M'},
		{"query", no_argument, NULL, 'Q'},
		{"sync", no_argument, NULL, 'S'},
		{"version", no_argument, NULL, 'V'},
		{"help", no_argument, NULL, 'h'},
		{"info", no_argument, NULL, 'i'},
		{"search", no_argument, NULL, 's'},
		{"upgrade", no_argument, NULL, 'u'},
		{"color", no_argument, NULL, OPT_COLOR},
		{"crawl", no_argument, NULL, PW_OP_CRAWL},
		{"debug", no_argument, NULL, OPT_DEBUG},
		{"nocolor", no_argument, NULL, OPT_NOCOLOR},
		{"vote", no_argument, NULL, OPT_SORT_VOTE},
		{"verbose", no_argument, NULL, OPT_VERBOSE},
		{"target", required_argument, NULL, OPT_TARGET_DIR},
		{"deps", no_argument, NULL, OPT_RESOLVE_DEPS},
		{"threads", required_argument, NULL, OPT_MAXTHREADS},
		{0, 0, 0, 0}
	};

	/* Get operation */
	while ((opt = getopt_long(argc, argv, optstring, opts, &option_index))) {
		if (opt < 0) {
			break;
		} else if (opt == 0) {
			continue;
		} else if (opt == '?') {
			return error(PW_ERR_OP_UNKNOWN);
		}

		parsearg_op(opt, 0);
	}

	if (config->op == PW_OP_INVAL) {
		return error(PW_ERR_OP_MULTI);
	} else if (config->version) {
		version();
	} else if (config->help) {
		usage(config->op);
	} else if (config->op == PW_OP_MAIN) {
		return error(PW_ERR_OP_NULL);
	}

	/* Parse remaining arguments */
	optind = 1;
	while ((opt = getopt_long(argc, argv, optstring, opts, &option_index))) {
		if (opt < 0) {
			break;
		} else if (opt == 0) {
			break;
			continue;
		} else if (opt == '?') {
			return error(PW_ERR_OP_UNKNOWN);
		} else if (parsearg_op(opt, 1) == 0) {
			/* Operation */
			continue;
		}

		switch (config->op) {
		case PW_OP_SYNC:
			res = parsearg_sync(opt);
			break;
		case PW_OP_QUERY:
			res = parsearg_query(opt);
			break;
		case PW_OP_GET:
			res = parsearg_get(opt);
			break;
		case PW_OP_MAINTAINER:
		case PW_OP_BACKUP:
		default:
			res = 1;
			break;
		}

		if (res == 0) {
			continue;
		}

		/* Parse global options */
		res = parsearg_global(opt);
		if (res != 0) {
			return error(PW_ERR_OP_UNKNOWN);
		}
	}

	/* GNU getopt permutes arguments. So leftover args are non-options
	 * and are taken to be packages
	 */
	while (optind < argc) {
		if (!alpm_list_find_str(powaur_targets, argv[optind])) {
			pw_printf(PW_LOG_DEBUG, "adding target: %s\n", argv[optind]);
			powaur_targets = alpm_list_add(powaur_targets, strdup(argv[optind]));
		}

		++optind;
	}

	return 0;
}

int main(int argc, char *argv[])
{
	int ret;

	if (setup_config()) {
		goto cleanup;
	}

	/* Parse arguments first so debug info can be up asap if enabled */
	ret = parseargs(argc, argv);
	if (ret) {
		goto cleanup;
	}

	/* If stdout is not terminal, turn off colourized output */
	if (!isatty(1)) {
		config->color = 0;
	}

	ret = powaur_init();
	ASSERT(ret == 0, goto cleanup);

	switch (config->op) {
	case PW_OP_GET:
		ret = powaur_get(powaur_targets);
		break;
	case PW_OP_SYNC:
		ret = powaur_sync(powaur_targets);
		break;
	case PW_OP_QUERY:
		ret = powaur_query(powaur_targets);
		break;
	case PW_OP_MAINTAINER:
		ret = powaur_maint(powaur_targets);
		break;
	case PW_OP_BACKUP:
		ret = powaur_backup(powaur_targets);
		break;
	case PW_OP_CRAWL:
		ret = powaur_crawl(powaur_targets);
		break;
	default:
		break;
	}

cleanup:
	powaur_cleanup(ret ? 1 : 0);
}
