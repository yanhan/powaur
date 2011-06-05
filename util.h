#ifndef POWAUR_UTIL_H
#define POWAUR_UTIL_H

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <alpm.h>

#include "error.h"
#include "powaur.h"
#include "environment.h"
#include "wrapper.h"

int powaur_backup(alpm_list_t *targets);

extern int (*pw_printf)(enum pwloglevel_t lvl, const char *fmt, ...)
__attribute__((format (printf, 2, 3)));

extern int (*pw_fprintf)(enum pwloglevel_t lvl, FILE *stream, const char *fmt, ...)
__attribute__((format (printf, 3, 4)));

extern int (*pw_vfprintf)(enum pwloglevel_t lvl, FILE *stream, const char *fmt, va_list ap)
__attribute__((format (printf, 3, 0)));

/* Setup color printing functions */
void color_print_setup(void);

/* Restore color printing functions to non-colorized versions */
void color_print_restore(void);

int extract_file(const char *filename);
int getcols(void);
char *strtrim(char *line);
/* Trims version from a string. ie, if you pass it "pacman>=3.5",
 * the string will become "pacman".
 *
 * @param line modifiable string
 */
char *strtrim_ver(char *line);
int wait_or_whine(pid_t pid, char *argv0);

/* Prints c for rep times */
void print_repeat(char c, int rep);
void indent_print(enum pwloglevel_t lvl, alpm_list_t *list, size_t indent);
void print_list(alpm_list_t *list, const char *prefix);
void print_list_break(alpm_list_t *list, const char *prefix);
void print_list_deps(alpm_list_t *list, const char *prefix);
void print_pkginfo(alpm_list_t *list);

/* Prints a list of aurpkg_t * */
void print_aurpkg_list(alpm_list_t *list);

/* Returns 1 for yes, 0 for no */
int yesno(const char *fmt, ...);

/* Asks the user a multiple choice question.
 * returns the index of array corresponding to user's answer.
 *
 * @param qn question to ask
 * @param array a character array of options
 * @param arraySize size of array
 * @param preset index of answer to return if user just presses Enter
 */
int mcq(const char *qn, const char *array, int arraySize, int preset);

char *have_dotinstall(void);
alpm_list_t *list_diff(alpm_list_t *left, alpm_list_t *right,
					   alpm_list_fn_cmp cmpfn);

alpm_list_t *list_intersect(alpm_list_t *left, alpm_list_t *right,
							alpm_list_fn_cmp cmpfn);

/* Returns a pmpkg_t * if given package is found in dbcache.
 * @param dbcache list of pmpkg_t *
 * @param pkgname package name
 */
pmpkg_t *cache_find_pkg(alpm_list_t *dbcache, const char *pkgname);

/* Returns a pmpkg_t * if given package is found in any of the dbs.
 * @param dbs list of pmdb_t *
 * @param pkgname package name
 */
pmpkg_t *dbs_find_pkg(alpm_list_t *dbs, const char *pkgname);

/* Prints the color of a repo */
void color_repo(const char *repo);

/* Prints groups in color */
void color_groups(alpm_list_t *grp);

/* sdbm string hashing function */
unsigned long sdbm(const char *str);

#define MINI_BUFSZ 60

#define ASSERT(somecond, someact) do {\
	if (!(somecond)) {\
		someact;\
	}\
} while (0)

#define PW_SETERRNO(errnum) do {\
	pwerrno = errnum;\
	pw_fprintf(PW_LOG_ERROR, stderr, "%s: %s\n", __func__, pw_strerrorlast());\
} while (0)

#define CLEAR_ERRNO() do {\
	pwerrno = PW_ERR_OK;\
} while (0)

#define RET_ERR(errnum, retval) do {\
	pwerrno = errnum;\
	pw_printf(PW_LOG_ERROR, "%s: %s\n", __func__, pw_strerror(errnum));\
	return retval;\
} while (0)

#define RET_ERR_VOID(errnum) do {\
	pwerrno = errnum;\
	return;\
} while (0)

#define CALLOC(myptr, nmemb, mysz, act) do {\
	myptr = xcalloc(nmemb, mysz);\
	if (!myptr) {\
		act;\
	}\
} while (0)

#define MALLOC(myptr, mysz, act) do {\
	myptr = malloc(mysz);\
	if (!myptr) {\
		act;\
	}\
} while (0)

#define STRDUP(mydest, myorig, act) do {\
	mydest = strdup(myorig);\
	if (!mydest) {\
		act;\
	}\
} while(0)

#define FREE(myptr) if (myptr) { free(myptr); }
#define FCLOSE(myfp) if (myfp) { fclose(myfp); myfp = NULL; }

#endif
