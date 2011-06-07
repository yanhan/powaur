#ifndef POWAUR_QUERY_H
#define POWAUR_QUERY_H

#include <alpm_list.h>
#include "graph.h"
#include "hashdb.h"

/* Search type */
enum aurquery_t {
	AUR_QUERY_SEARCH,
	AUR_QUERY_INFO,
	AUR_QUERY_MSEARCH
};

/* Builds a dependency graph for the given package.
 *
 * @param hashdb hash database
 * @param pkgname package name
 * @param topost if given, topological order of graph will be stored here
 * @param cycles if given, set to 1 if graph has cycles, 0 otherwise
 */
struct graph *build_dep_graph(struct pw_hashdb *hashdb, const char *pkgname,
							  struct stack *topost, int *cycles);
int powaur_query(alpm_list_t *targets);
int powaur_crawl(alpm_list_t *targets);

#endif
