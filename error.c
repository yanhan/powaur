#include "environment.h"
#include "error.h"
#include "powaur.h"

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
	case PW_ERR_INIT_LOCAL_DB:
		return "Failed to initialize local db";
	
	/* Command parsing errors */
	case PW_ERR_OP_UNKNOWN:
		return "Unknown option";
	case PW_ERR_OP_MULTI:
		return "Multiple operations not allowed";

	case PW_ERR_PM_CONF_OPEN:
		return "Error opening /etc/pacman.conf";
	case PW_ERR_PM_CONF_PARSE:
		return "Error parsing /etc/pacman.conf";
	
	/* Fatal errors */
	case PW_ERR_ACCESS:
		return "Insufficient permissions to write to directory";

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
	case PW_ERR_GETCWD:
		return "Error getting CWD";
	case PW_ERR_RESTORECWD:
		return "Error restoring CWD";
	case PW_ERR_ISDIR:
		return "Writing to an existing directory";
	case PW_ERR_FOPEN:
		return "Error opening file";
	case PW_ERR_FILE_EXTRACT:
		return "File extraction failed";
	case PW_ERR_OPENDIR:
		return "Failed to open directory";
	case PW_ERR_STAT:
		return "Failed to stat file/directory";
	case PW_ERR_PATH_RESOLVE:
		return "Failed to resolve path";

	/* fork errors */
	case PW_ERR_FORK_FAILED:
		return "Forking of process failed";
	case PW_ERR_WAITPID_FAILED:
		return "Failed to wait for child process";
	case PW_ERR_WAITPID_CONFUSED:
		return "waitpid is confused";

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
		return "No target packages";

	default:
		return "Unknown error";
	}
}
