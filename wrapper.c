#include <stdlib.h>
#include <string.h>

#include "error.h"
#include "powaur.h"
#include "wrapper.h"

/* Simple wrappers around some functions */

void *xcalloc(size_t nmemb, size_t sz)
{
	void *ret = calloc(nmemb, sz);
	if (!ret) {
		die_errno(PW_ERR_MEMORY);
	}

	return ret;
}

void *xmalloc(size_t sz)
{
	void *ret = malloc(sz);
	if (!ret) {
		die_errno(PW_ERR_MEMORY);
	}

	return ret;
}

char *xstrdup(const char *str)
{
	char *ret = strdup(str);
	if (!ret) {
		die_errno(PW_ERR_MEMORY);
	}

	return ret;
}
