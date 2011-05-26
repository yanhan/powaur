#include <limits.h>
#include <string.h>

#include <yajl/yajl_parse.h>

#include "curl.h"
#include "environment.h"
#include "error.h"
#include "handle.h"
#include "json.h"
#include "powaur.h"
#include "query.h"
#include "util.h"

yajl_handle yajl_init(void)
{
	/* Reset the json_ctx */
	pwhandle->json_ctx->pkglist = NULL;
	pwhandle->json_ctx->curpkg = NULL;
	pwhandle->json_ctx->jsondepth = 0;

	yajl_handle yajl_hand = yajl_alloc(yajl_cbs, NULL, pwhandle->json_ctx);
	if (!yajl_hand) {
		die_errno(PW_ERR_MEMORY);
	}

	return yajl_hand;
}

/* Issues a query to AUR.
 * @param pkgname package to query
 * @param type type of query: info, search, msearch
 * returns an alpm_list_t * of packages if everything is ok,
 * otherwise, returns NULL.
 */
alpm_list_t *query_aur(CURL *curl, const char *searchstr, enum aurquery_t query_type)
{
	int ret = 0;
	yajl_handle hand;
	char url[PATH_MAX];
	long httpresp;

	hand = yajl_init();

	/* Query AUR */
	curl_reset(curl);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, parse_json);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, hand);

	switch (query_type) {
	case AUR_QUERY_SEARCH:
		snprintf(url, PATH_MAX, AUR_RPC_URL, AUR_RPC_TYPE_SEARCH, searchstr);
		break;
	
	case AUR_QUERY_INFO:
		snprintf(url, PATH_MAX, AUR_RPC_URL, AUR_RPC_TYPE_INFO, searchstr);
		break;
	
	case AUR_QUERY_MSEARCH:
		snprintf(url, PATH_MAX, AUR_RPC_URL, AUR_RPC_TYPE_MSEARCH, searchstr);
		break;
	
	default:
		break;
	}

	curl_easy_setopt(curl, CURLOPT_URL, url);
	if (curl_easy_perform(curl) != CURLE_OK) {
		yajl_free(hand);

		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpresp);
		if (httpresp != 200) {
			pw_fprintf(PW_LOG_ERROR, stderr, "curl responded with http code %ld",
					   httpresp);
		}

		RET_ERR(PW_ERR_CURL_DOWNLOAD, NULL);
	}

	yajl_complete_parse(hand);
	yajl_free(hand);
	return pwhandle->json_ctx->pkglist;
}

/* yajl callback functions */
static int json_string(void *ctx, const unsigned char *ukey, size_t len)
{
	struct json_ctx_t *parser = ctx;
	const char *key = ukey;

	if (strcmp(parser->curkey, "type") == 0 &&
		strncmp(key, "error", 5) == 0) {
		return 1;
	} else if (strcmp(parser->curkey, "ID") == 0) {
		parser->curpkg->id = strndup(key, len);
	} else if (strcmp(parser->curkey, "Name") == 0) {
		parser->curpkg->name = strndup(key, len);
	} else if (strcmp(parser->curkey, "Version") == 0) {
		parser->curpkg->version = strndup(key, len);
	} else if (strcmp(parser->curkey, "CategoryID") == 0) {
		parser->curpkg->category = strndup(key, len);
	} else if (strcmp(parser->curkey, "Description") == 0) {
		parser->curpkg->desc = strndup(key, len);
	} else if (strcmp(parser->curkey, "URL") == 0) {
		parser->curpkg->url = strndup(key, len);
	} else if (strcmp(parser->curkey, "URLPath") == 0) {
		parser->curpkg->urlpath = strndup(key, len);
	} else if (strcmp(parser->curkey, "License") == 0) {
		parser->curpkg->license = strndup(key, len);
	} else if (strcmp(parser->curkey, "NumVotes") == 0) {
		parser->curpkg->votes = atoi(key);
	} else if (strcmp(parser->curkey, "OutOfDate") == 0) {
		parser->curpkg->outofdate = atoi(key);
	}

	return 1;
}

static int json_start_map(void *ctx)
{
	struct json_ctx_t *parser = ctx;
	if (parser->jsondepth++ > 0) {
		parser->curpkg = aurpkg_new();
	}

	return 1;
}

static int json_map_key(void *ctx, const unsigned char *ukey, size_t len)
{
	struct json_ctx_t *parser = ctx;
	strncpy(parser->curkey, ukey, len);
	parser->curkey[len] = 0;

	return 1;
}

static int json_end_map(void *ctx)
{
	struct json_ctx_t *parser = ctx;
	if (--parser->jsondepth > 0) {
		parser->pkglist = alpm_list_add(parser->pkglist, parser->curpkg);
		parser->curpkg = NULL;
	}
	
	return 1;
}

/* yajl_callback functions.
 * They handle the "events" of yajl.
 */
yajl_callbacks yajl_cbs[] = {
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	json_string,
	json_start_map,
	json_map_key,
	json_end_map,
	NULL,
	NULL
};


/* curl WRITEDATA function */
size_t parse_json(void *ptr, size_t sz, size_t nmemb, void *userdata)
{
	size_t totalsz = sz * nmemb;
	yajl_handle hand = userdata;

	yajl_parse(hand, ptr, totalsz);
	return totalsz;
}
