#include <curl/curl.h>

#include "curl.h"
#include "util.h"

static int initialized = 0;

int curl_init(void)
{
	if (!initialized) {
		curl_global_init(CURL_GLOBAL_ALL);
		initialized = 1;
	}

	return !initialized;
}

void curl_cleanup(void)
{
	if (initialized) {
		curl_global_cleanup();
	}
}

CURL *curl_easy_new(void)
{
	CURL *curl = curl_easy_init();
	if (!curl) {
		return NULL;
	}

	curl_reset(curl);
	return curl;
}

void curl_reset(CURL *curl)
{
	curl_easy_reset(curl);

	/* TODO: Shift this to conf file */
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
}
