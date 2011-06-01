#ifndef POWAUR_WRAPPER_H
#define POWAUR_WRAPPER_H

void *xcalloc(size_t nmemb, size_t sz);
void *xmalloc(size_t sz);
void *xrealloc(void *data, size_t sz);
char *xstrdup(const char *str);

#endif
