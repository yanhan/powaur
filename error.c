#include <limits.h>
#include <stdarg.h>

#include "environment.h"
#include "error.h"
#include "powaur.h"
#include "util.h"

int error(enum _pw_errno_t err, ...)
{
	char buf[PATH_MAX];
	va_list args;

	pwerrno = err;
	va_start(args, err);
	vsnprintf(buf, PATH_MAX, pw_strerror(err), args);
	fprintf(stderr, "error: %s\n", buf);
	va_end(args);

	return -1;
}

void die(const char *msg, ...)
{
	char buf[PATH_MAX];
	va_list ap;
	va_start(ap, msg);

	vsnprintf(buf, PATH_MAX, msg, ap);
	fprintf(stderr, "fatal: %s\n", buf);

	va_end(ap);
	exit(1);
}

void die_errno(enum _pw_errno_t err, ...)
{
	pwerrno = err;

	char buf[PATH_MAX];
	va_list ap;
	va_start(ap, err);

	vsnprintf(buf, PATH_MAX, pw_strerror(err), ap);
	fprintf(stderr, "fatal: %s\n", buf);

	va_end(ap);
	exit(1);
}

const char *pw_strerrorlast(void)
{
	return pw_strerror(pwerrno);
}

const char *pw_strerror(enum _pw_errno_t err)
{
	switch (err) {
	case PW_ERR_INIT_CONFIG:
		return "config initialization failed";
	case PW_ERR_INIT_ENV:
		return "Setup environment failed";
	case PW_ERR_INIT_HANDLE:
		return "Handle initialization failed";
	case PW_ERR_INIT_DIR:
		return "Failed to setup powaur_dir";
	case PW_ERR_INIT_LOCALDB:
		return "Failed to initialize local db";

	/* Command parsing errors */
	case PW_ERR_OP_UNKNOWN:
		return "Unknown option";
	case PW_ERR_OP_MULTI:
		return "Multiple operations not allowed";
	case PW_ERR_OP_NULL:
		return "no operation specified (use -h for help)";

	case PW_ERR_PM_CONF_OPEN:
		return "Error opening /etc/pacman.conf";
	case PW_ERR_PM_CONF_PARSE:
		return "Error parsing /etc/pacman.conf";

	/* Fatal errors */
	case PW_ERR_ACCESS:
		return "Insufficient permissions to write to directory %s";

	/* libalpm errors */
	case PW_ERR_ALPM_RELOAD:
		return "Failed to reload libalpm";
	case PW_ERR_LOCALDB_NULL:
		return "localdb is NULL";
	case PW_ERR_LOCALDB_CACHE_NULL:
		return "localdb pkgcache is NULL";

	/* General errors */
	case PW_ERR_MEMORY:
		return "Memory allocation error";

	/* Path related errors */
	case PW_ERR_GETCWD:
		return "Error getting CWD";
	case PW_ERR_RESTORECWD:
		return "Error restoring CWD";
	case PW_ERR_CHDIR:
		return "Error changing dir to %s";
	case PW_ERR_PATH_RESOLVE:
		return "Failed to resolve path: %s";

	/* File related errors */
	case PW_ERR_ISDIR:
		return "Name clash with existing directory \"%s\"";
	case PW_ERR_FOPEN:
		return "Error opening file %s";
	case PW_ERR_FILE_EXTRACT:
		return "File extraction failed";
	case PW_ERR_OPENDIR:
		return "Failed to open directory";
	case PW_ERR_STAT:
		return "Failed to stat %s";

	/* Fork errors */
	case PW_ERR_FORK_FAILED:
		return "Forking of process failed";
	case PW_ERR_WAITPID_FAILED:
		return "Failed to wait for child process";
	case PW_ERR_WAITPID_CONFUSED:
		return "waitpid is confused";

	/* pthreads errors */
	case PW_ERR_PTHREAD_CREATE:
		return "thread creation failed";
	case PW_ERR_PTHREAD_JOIN:
		return "thread joining failed";

	/* libarchive errors */
	case PW_ERR_ARCHIVE_CREATE:
		return "Fail to initialize archive";
	case PW_ERR_ARCHIVE_OPEN:
		return "Fail to open archive";
	case PW_ERR_ARCHIVE_ENTRY:
		return "Fail to create archive entry";
	case PW_ERR_ARCHIVE_EXTRACT:
		return "Fail to extract file from archive";

	/* cURL errors */
	case PW_ERR_CURL_INIT:
		return "cURL initialization failed";
	case PW_ERR_CURL_DOWNLOAD:
		return "cURL download failed";

	/* Download errors */
	case PW_ERR_DL_UNKNOWN:
		return "Unknown package";

	/* Search errors */
	case PW_ERR_TARGETS_NULL:
		return "No package specified for %s";

	default:
		return "Unknown error";
	}
}
