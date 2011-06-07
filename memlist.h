#ifndef POWAUR_MEMLIST_H
#define POWAUR_MEMLIST_H

struct memlist_node {
	void *data;
	unsigned int nr;
	struct memlist_node *next;
};

/* Provides a list of fixed size memory pools */
struct memlist {
	struct memlist_node *pool;
	unsigned int elemSz;
	unsigned int max_elems;
	int free_inner;
};

enum {
	MEMLIST_NORM = 0,
	MEMLIST_PTR
};

struct memlist *memlist_new(unsigned int max_elems, unsigned int elemSz, int free_inner);
/* Returns the final location of data in memlist after addition */
void *memlist_add(struct memlist *memlist, void *data);
void memlist_free(struct memlist *memlist);

#endif
