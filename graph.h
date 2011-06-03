#ifndef POWAUR_GRAPH_H
#define POWAUR_GRAPH_H

#include "hash.h"

/* Opaque */
struct vertex;

/* Forward declaration */
struct stack;

/* Graph data structure.
 * vidx - hash table used to query index of a given vertex
 */
struct graph {
	struct vertex *vertices;
	struct hash_table *vidx;
	int nr;
	int sz;
};

struct graph *graph_new(unsigned long (*hash_fn) (void *),
						int (*cmp_fn) (const void *, const void *));

void graph_free(struct graph *g);

/* Adds a new vertex into the graph */
void graph_add_vertex(struct graph *g, void *data);

/* Adds a new edge to the graph, from->to
 * Will add vertices "from" and "to" if they are not in the graph
 */
void graph_add_edge(struct graph *graph, void *from, void *to);

/* Returns the data of vertex indexed at pos if it exists, NULL otherwise */
void *graph_get_vertex_data(struct graph *graph, int pos);

/* Does a topological sort of the graph, with cycle detection.
 * returns -1 if cycles are detected, 0 otherwise.
 *
 * @param graph graph to perform toposort on
 * @param topost stack of int to store topological order
 */
int graph_toposort(struct graph *graph, struct stack *topost);


/* Stack */
struct stack {
	void *st;
	int nr;
	int sz;
	size_t elemSz;
};

struct stack *stack_new(size_t elemSz);
void stack_free(struct stack *st);
int stack_empty(struct stack *st);
void stack_reset(struct stack *st);
void stack_push(struct stack *st, void *data);
void stack_pop(struct stack *st, void *data);
void stack_peek(struct stack *st, void *data);

#endif
