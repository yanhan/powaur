#ifndef POWAUR_ENV_H
#define POWAUR_ENV_H

#include <alpm_list.h>

#include "conf.h"
#include "powaur.h"

#define AUR_URL "http://aur.archlinux.org"
#define AUR_PKG_URL "http://aur.archlinux.org/packages.php?ID=%s"
#define AUR_PKGTAR_URL "http://aur.archlinux.org/packages/%s/%s.tar.gz"
#define AUR_PKGBUILD_URL "http://aur.archlinux.org/packages/%s/PKGBUILD"
#define AUR_RPC_URL "http://aur.archlinux.org/rpc.php?type=%s&arg=%s"

#define AUR_RPC_TYPE_INFO "info"
#define AUR_RPC_TYPE_MSEARCH "msearch"
#define AUR_RPC_TYPE_SEARCH "search"

/* powaur defaults */
#define PW_DEF_DIR "/tmp/powaur/"
#define PW_DEF_EDITOR "vim"
#define PW_CONF "powaur.conf"

/* Pacman defaults */
#define PACMAN_DEF_ROOTDIR "/"
#define PACMAN_DEF_DBPATH "/var/lib/pacman/"
#define PACMAN_DEF_CACHEDIR "/var/cache/pacman/pkg/"

/* Global configuration */
extern struct config_t *config;

/* Powaur environment */
extern enum _pw_errno_t pwerrno;
extern char *powaur_dir;
extern char *powaur_editor;

/* Pacman configuration settings */
extern char *pacman_rootdir;
extern char *pacman_dbpath;
extern alpm_list_t *pacman_cachedirs;

/* For commonly used strings */
struct commonstrings {
	const char *myname;
	const char *usage;
	const char *pkg;
	const char *opt;
	const char *tab;

	/* Pacman configuration */
	const char *pmconf;
	const char *rootdir;
	const char *dbpath;
	const char *cachedir;

	/* -Si, -Qi */
	const char *i_repo;
	const char *i_name;
	const char *i_version;
	const char *i_url;
	const char *i_licenses;
	const char *i_grps;
	const char *i_provides;
	const char *i_deps;
	const char *i_optdeps;
	const char *i_reqby;
	const char *i_conflicts;
	const char *i_replaces;
	const char *i_dlsz;
	const char *i_isz;
	const char *i_pkger;
	const char *i_arch;
	const char *i_builddate;
	const char *i_installdate;
	const char *i_reason;
	const char *i_script;
	const char *i_md5;
	const char *i_desc;

	/* -Si, -Qi for AUR */
	const char *i_aur_url;
	const char *i_aur_votes;
	const char *i_aur_outofdate;
};

extern struct commonstrings comstrs;

int setup_environment(void);
void cleanup_environment(void);
int alpm_reload(void);

#endif
