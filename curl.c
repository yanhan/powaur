#include "curl.h"
#include "util.h"

static int initialized = 0;
CURL *curl = NULL;

void curl_init(void)
{
	if (!initialized) {
		curl_global_init(CURL_GLOBAL_ALL);
		curl = curl_easy_init();

		if (!curl) {
			die_errno(PW_ERR_CURL_INIT);
		}

		initialized = 1;

	} else {
		curl_easy_reset(curl);
		/* Set timeout */
		curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
		curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	}
}

void curl_cleanup(void)
{
	if (initialized) {
		curl_easy_cleanup(curl);
		curl_global_cleanup();
	}
}
