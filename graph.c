#include <string.h>

#include "graph.h"
#include "wrapper.h"
#include "hash.h"

#define GRAPH_INIT_VERTICES 40
#define VINDEX_INIT_SZ 109
#define STACK_INIT_SZ 40

/* Graph index data structures
 * Used internally inside struct graph
 */

struct vertex {
	void *data;
	int *adj;
	int nr;
	int sz;
	int dfs_idx;

	enum {
		WHITE, GRAY, BLACK
	} color;
};

/* struct vertex functions */

#define VERTEX_ADJ_INIT_SZ 20
static void vertex_free(struct vertex *vertex)
{
	if (!vertex) {
		return;
	}

	free(vertex->adj);
}

static void vertex_reset(struct vertex *vertex)
{
	vertex->data = NULL;
	vertex->adj = NULL;
	vertex->nr = 0;
	vertex->sz = 0;
	vertex->dfs_idx = 0;
	vertex->color = WHITE;
}

static void vertex_init(struct vertex *vertex)
{
	vertex_reset(vertex);
	vertex->adj = xcalloc(VERTEX_ADJ_INIT_SZ, sizeof(int));
	vertex->sz = VERTEX_ADJ_INIT_SZ;
}

static void vertex_dfs_reset(struct vertex *vertex)
{
	vertex->color = WHITE;
	vertex->dfs_idx = 0;
}

/* Add an edge to the vertex */
static void vertex_add_edge(struct vertex *vertex, int edge)
{
	int i;
	for (i = 0; i < vertex->nr; ++i) {
		if (edge == vertex->adj[i]) {
			return;
		}
	}

	if (vertex->nr >= vertex->sz) {
		int new_size = new_alloc_size(vertex->sz);
		if (new_size < vertex->sz) {
			die("vertex_add_edge: adjacency list size exceeded");
		}

		vertex->adj = xrealloc(vertex->adj, new_size * sizeof(int));
		vertex->sz = new_size;
	}

	vertex->adj[vertex->nr++] = edge;
}

static struct vertex *vertex_get_next_edge(struct graph *graph, struct vertex *vertex)
{
	return &(graph->vertices[vertex->adj[vertex->dfs_idx++]]);
}

/* Used to determine data is located in the graph's adj list
 * returns data's position in graph's adj list if exists, -1 otherwise.
 */
static int vindex_lookup(struct graph *graph, void *data)
{
	return hash_pos(graph->vidx, data);
}

struct graph *graph_new(unsigned long (*hash_fn) (void *),
						int (*cmp_fn) (const void *, const void *))
{
	struct graph *graph;
	graph = xcalloc(1, sizeof(struct graph));
	graph->nr = 0;
	graph->sz = GRAPH_INIT_VERTICES;
	graph->vertices = xcalloc(GRAPH_INIT_VERTICES, sizeof(struct vertex));
	graph->vidx = hash_new(VINDEX, hash_fn, cmp_fn);

	int i;
	for (i = 0; i < GRAPH_INIT_VERTICES; ++i) {
		vertex_reset(&graph->vertices[i]);
	}

	return graph;
}

void graph_free(struct graph *graph)
{
	if (!graph) {
		return;
	}

	int i;
	for (i = 0; i < graph->nr; ++i) {
		vertex_free(&graph->vertices[i]);
	}

	hash_free(graph->vidx);
	free(graph->vertices);
	free(graph);
}

static void graph_grow(struct graph *graph)
{
	int new_size = new_alloc_size(graph->sz);
	graph->vertices = xrealloc(graph->vertices, new_size * sizeof(struct vertex));
	graph->sz = new_size;
}

void graph_add_vertex(struct graph *graph, void *data)
{
	if (vindex_lookup(graph, data) != -1) {
		return;
	}

	if (graph->nr >= graph->sz) {
		graph_grow(graph);
	}

	vertex_init(&graph->vertices[graph->nr]);
	graph->vertices[graph->nr].data = data;

	/* Required for vindex */
	struct vidx_node node;
	node.data = data;
	node.idx = graph->nr;
	hash_insert(graph->vidx, &node);
	graph->nr++;
}

void graph_add_edge(struct graph *graph, void *from, void *to)
{
	/* Make sure both from and to actually exist, otherwise add */
	graph_add_vertex(graph, from);
	graph_add_vertex(graph, to);

	int from_pos = vindex_lookup(graph, from);
	int to_pos = vindex_lookup(graph, to);

	vertex_add_edge(&graph->vertices[from_pos], to_pos);
}

/* returns 0 on no cycle, -1 on cycle */
int graph_dfs(struct graph *graph, int root, struct stack *topost)
{
	graph->vertices[root].color = GRAY;
	struct vertex *curv, *next;
	struct stack *st = stack_new(sizeof(struct vertex *));
	int ret = 0;
	int pos;

	curv = graph->vertices + root;
	stack_push(st, &curv);
	while (!stack_empty(st)) {
		stack_peek(st, &curv);
		if (curv->dfs_idx >= curv->nr) {
			curv->color = BLACK;
			pos = vindex_lookup(graph, curv->data);
			stack_push(topost, &pos);
			stack_pop(st, &curv);
			continue;
		}

		next = vertex_get_next_edge(graph, curv);
		if (next->color == GRAY) {
			ret = -1;
			break;
		} else if (next->color == WHITE) {
			next->color = GRAY;
			stack_push(st, &next);
		}
	}

	stack_free(st);
	return ret;
}

void *graph_get_vertex_data(struct graph *graph, int pos)
{
	if (pos < 0 || pos >= graph->nr) {
		return NULL;
	}

	return graph->vertices[pos].data;
}

int graph_toposort(struct graph *graph, struct stack *topost)
{
	/* Reset color */
	int i, cycle = 0;
	for (i = 0; i < graph->nr; ++i) {
		vertex_dfs_reset(graph->vertices + i);
	}

	for (i = 0; i < graph->nr; ++i) {
		if (graph->vertices[i].color == WHITE) {
			cycle = graph_dfs(graph, i, topost);
			if (cycle) {
				break;
			}
		}
	}

	return cycle;
}

struct stack *stack_new(size_t elemSz)
{
	struct stack *stack = xcalloc(1, sizeof(struct stack));
	stack->elemSz = elemSz;
	stack->sz = STACK_INIT_SZ;
	stack->st = xcalloc(STACK_INIT_SZ, elemSz);

	return stack;
}

void stack_free(struct stack *st)
{
	if (!st) {
		return;
	}

	free(st->st);
	free(st);
}

int stack_empty(struct stack *st)
{
	return st->nr <= 0;
}

void stack_reset(struct stack *st)
{
	st->nr = 0;
}

static void stack_grow(struct stack *st)
{
	int new_size = st->sz * 2;
	st->st = xrealloc(st->st, new_size * st->elemSz);
	st->sz = new_size;
}

void stack_push(struct stack *st, void *data)
{
	if (st->nr >= st->sz) {
		stack_grow(st);
	}

	memcpy((char *) st->st + st->elemSz * st->nr++, data, st->elemSz);
}

void stack_pop(struct stack *st, void *data)
{
	if (st->sz <= 0) {
		return;
	}

	memcpy(data, (char *) st->st + --st->nr * st->elemSz, st->elemSz);
}

void stack_peek(struct stack *st, void *data)
{
	if (st->nr <= 0) {
		return;
	}

	memcpy(data, (char *) st->st + (st->nr-1) * st->elemSz, st->elemSz);
}
