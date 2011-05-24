#ifndef POWAUR_CURL_H
#define POWAUR_CURL_H

#include <curl/curl.h>

void curl_init(void);
void curl_cleanup(void);

extern CURL *curl;

#endif
