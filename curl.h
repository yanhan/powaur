#ifndef POWAUR_CURL_H
#define POWAUR_CURL_H

#include <curl/curl.h>

int curl_init(void);
void curl_cleanup(void);
CURL *curl_easy_new(void);
void curl_reset(CURL *curl);

#endif
