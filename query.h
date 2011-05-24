#ifndef POWAUR_QUERY_H
#define POWAUR_QUERY_H

#include <alpm_list.h>

/* Search type */
enum aurquery_t {
	AUR_QUERY_SEARCH,
	AUR_QUERY_INFO,
	AUR_QUERY_MSEARCH
};

int powaur_query(alpm_list_t *targets);

#endif
