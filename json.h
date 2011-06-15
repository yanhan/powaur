#ifndef POWAUR_JSON_H
#define POWAUR_JSON_H

#include <alpm.h>
#include <curl/curl.h>
#include <yajl/yajl_parse.h>

#include "query.h"
#include "package.h"

#define JSON_KEY_LEN 40

/* This is the void *ctx passed into yajl_alloc(), which is in turn
 * passed to the yajl_callbacks when yajl_parse() is called.
 */

struct json_ctx_t {
	alpm_list_t *pkglist;
	struct aurpkg_t *curpkg;
	char curkey[JSON_KEY_LEN];
	int jsondepth;
};

/* Query functions */
alpm_list_t *query_aur(CURL *curl, const char *pkgname, enum aurquery_t type);

/* curl WRITEDATA function */
size_t parse_json(void *ptr, size_t sz, size_t nmemb, void *userdata);

extern yajl_callbacks yajl_cbs[];

#endif
