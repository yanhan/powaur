#ifndef POWAUR_ENV_H
#define POWAUR_ENV_H

#include <alpm_list.h>

#include "argv-array.h"
#include "conf.h"
#include "powaur.h"

#define AUR_URL          "http://aur.archlinux.org"
#define AUR_PKG_URL      "http://aur.archlinux.org/packages.php?ID=%s"
#define AUR_PKGTAR_URL   "http://aur.archlinux.org/packages/%s/%s.tar.gz"
#define AUR_PKGBUILD_URL "http://aur.archlinux.org/packages/%s/PKGBUILD"
#define AUR_RPC_URL      "http://aur.archlinux.org/rpc.php?type=%s&arg=%s"

#define AUR_RPC_TYPE_INFO    "info"
#define AUR_RPC_TYPE_MSEARCH "msearch"
#define AUR_RPC_TYPE_SEARCH  "search"

/* powaur defaults */
#define PW_DEF_DIR        "/tmp/powaur"
#define PW_DEF_EDITOR     "vim"
#define PW_CONF           "powaur.conf"
#define PW_DEF_MAXTHREADS 10

/* Pacman defaults */
#define PACMAN_DEF_ROOTDIR  "/"
#define PACMAN_DEF_DBPATH   "/var/lib/pacman/"
#define PACMAN_DEF_CACHEDIR "/var/cache/pacman/pkg/"

/* Strings */
#define MYNAME      "powaur"
#define USAGE       "usage: "
#define PKG         "package(s)"
#define OPT         "options"
#define TAB         "    "
#define PMCONF      "/etc/pacman.conf"
#define ROOTDIR     "RootDir"
#define DBPATH      "DBPath"
#define CACHEDIR    "CacheDir"
#define REPO        "Repository     :"
#define NAME        "Name           :"
#define VERSION     "Version        :"
#define URL         "URL            :"
#define LICENSES    "Licenses       :"
#define GROUPS      "Groups         :"
#define PROVIDES    "Provides       :"
#define DEPS        "Depends On     :"
#define OPTDEPS     "Optional Deps  :"
#define REQBY       "Required By    :"
#define CONFLICTS   "Conflicts With :"
#define REPLACES    "Replaces       :"
#define DLSZ        "Download Size  :"
#define INSTSZ      "Installed Size :"
#define PKGER       "Packager       :"
#define ARCH        "Architecture   :"
#define BDATE       "Build Date     :"
#define IDATE       "Install Date   :"
#define REASON      "Install Reason :"
#define SCRIPT      "Install Script :"
#define MD5SUM      "MD5 Sum        :"
#define DESC        "Description    :"
#define A_URL       "AUR URL        :"
#define A_VOTES     "Votes          :"
#define A_OUTOFDATE "Out of Date    :"
#define LOCAL       "local"

/* Colors */
#define BLACK    "\033[0;30m"
#define RED      "\033[0;31m"
#define GREEN    "\033[0;32m"
#define YELLOW   "\033[0;33m"
#define BLUE     "\033[0;34m"
#define MAG      "\033[0;35m"
#define CYAN     "\033[0;36m"
#define WHITE    "\033[0;37m"

#define BBLACK   "\033[1;30m"
#define BRED     "\033[1;31m"
#define BGREEN   "\033[1;32m"
#define BYELLOW  "\033[1;33m"
#define BBLUE    "\033[1;34m"
#define BMAG     "\033[1;35m"
#define BCYAN    "\033[1;36m"
#define BWHITE   "\033[2;37m"

#define NOCOLOR  "\033[0m"
#define BOLD     "\033[1m"
#define VOTECOL  BYELLOW

struct colorstrs {
	char *nocolor;
	char *bold;
	char *votecol;
	char *black;
	char *red;
	char *green;
	char *yellow;
	char *blue;
	char *mag;
	char *cyan;
	char *white;
	char *bblack;
	char *bred;
	char *bgreen;
	char *byellow;
	char *bblue;
	char *bmag;
	char *bcyan;
	char *bwhite;
};

extern struct colorstrs color;

/* Global configuration */
extern struct config_t *config;

/* Powaur environment */
extern enum _pw_errno_t pwerrno;
extern char *powaur_dir;
extern char *powaur_editor;
extern int powaur_maxthreads;
extern struct argv_array powaur_makepkg_argv;

/* Pacman configuration settings */
extern char *pacman_rootdir;
extern char *pacman_dbpath;
extern alpm_list_t *pacman_cachedirs;

int setup_environment(void);
void colors_setup(void);
void cleanup_environment(void);

#endif
