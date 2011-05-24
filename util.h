#ifndef POWAUR_UTIL_H
#define POWAUR_UTIL_H

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <alpm_list.h>

#include "error.h"
#include "powaur.h"
#include "environment.h"

int powaur_backup(alpm_list_t *targets);

int pw_printf(enum pwloglevel_t lvl, char *fmt, ...)
__attribute__((format (printf, 2, 3)));

int pw_fprintf(enum pwloglevel_t lvl, FILE *stream, char *fmt, ...)
__attribute__((format (printf, 3, 4)));

int pw_vfprintf(enum pwloglevel_t lvl, FILE *stream, char *fmt, va_list ap)
__attribute__((format (printf, 3, 0)));

int extract_file(const char *filename);
int getcols(void);
char *strtrim(char *line);
int wait_or_whine(pid_t pid, char *argv0);

void indent_print(enum pwloglevel_t lvl, alpm_list_t *list, size_t indent);
void print_list(alpm_list_t *list, const char *prefix);
void print_list_break(alpm_list_t *list, const char *prefix);
void print_list_deps(alpm_list_t *list, const char *prefix);
void print_pkginfo(alpm_list_t *list);

int yesno(const char *fmt, ...);
int mcq(const char *qn, const char *array, int arraySize, int preset);

char *have_dotinstall(void);
alpm_list_t *list_diff(alpm_list_t *left, alpm_list_t *right,
					   alpm_list_fn_cmp cmpfn);

alpm_list_t *list_intersect(alpm_list_t *left, alpm_list_t *right,
							alpm_list_fn_cmp cmpfn);

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
	myptr = calloc(nmemb, mysz);\
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
