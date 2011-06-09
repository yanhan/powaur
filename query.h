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

/* For dep graph purposes */
enum {
	GRAPH_NOT_DETAILED = 0,
	GRAPH_DETAILED
};

enum {
	NOFORCE = 0,
	FORCE_DL
};

/* Builds a dependency graph for the given package.
 *
 * @param graph if graph is existing, will add on to the graph.
 * @param hashdb hash database
 * @param targets list of strings (packages)
 * @param detailed if given, show detailed dependencies
 * @param force force downloading of AUR packages
 *
 */
void build_dep_graph(struct graph **graph, struct pw_hashdb *hashdb,
					 alpm_list_t *targets, int detailed, int force);

/* Prints topological order of graph of strings
 *
 * @param graph graph of strings
 * @param topost stack of integers containing topological order of graph
 */
void print_topo_order(struct graph *graph, struct stack *topost);

int powaur_query(alpm_list_t *targets);
int powaur_crawl(alpm_list_t *targets);

#endif
