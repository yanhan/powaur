#include <string.h>

#include "memlist.h"
#include "wrapper.h"

struct memlist *memlist_new(unsigned int max_elems, unsigned int elemSz, int free_inner)
{
	struct memlist *memlist = xcalloc(1, sizeof(struct memlist));
	memlist->max_elems = max_elems;
	memlist->elemSz = elemSz;
	memlist->pool = xcalloc(1, sizeof(struct memlist_node));
	memlist->pool->data = xcalloc(max_elems, elemSz);

	if (free_inner != MEMLIST_NORM && free_inner != MEMLIST_PTR) {
		free_inner = MEMLIST_NORM;
	}
	memlist->free_inner = free_inner;
	return memlist;
}

void *memlist_add(struct memlist *memlist, void *data)
{
	struct memlist_node *curpool = memlist->pool;
	if (curpool->nr >= memlist->max_elems) {
		curpool = xcalloc(1, sizeof(struct memlist_node));
		curpool->data = xcalloc(memlist->max_elems, memlist->elemSz);
		curpool->next = memlist->pool;
		memlist->pool = curpool;
	}

	void *dest = (char *) curpool->data + memlist->elemSz * curpool->nr;
	memcpy(dest, data, memlist->elemSz);
	curpool->nr++;

	/* NOTE: free_inner assumes that we are storing pointers here. So we will
	 * dereference the data when free_inner is set.
	 */
	return memlist->free_inner ? *(void **) dest : dest;
}

static void memlist_free_inner(struct memlist *memlist)
{
	struct memlist_node *cur;
	unsigned int i;
	while (memlist->pool) {
		cur = memlist->pool;
		memlist->pool = memlist->pool->next;

		for (i = 0; i < cur->nr; ++i) {
			/* Ugly casting but can't be helped.
			 * Here, the memory pool is storing pointers which need to be freed,
			 * hence the void ** cast followed by dereference
			 */
			free(*(void **) ((char *) cur->data + i * memlist->elemSz));
		}

		free(cur->data);
		free(cur);
	}

	free(memlist);
}

void memlist_free(struct memlist *memlist)
{
	if (memlist->free_inner) {
		memlist_free_inner(memlist);
		return;
	}

	struct memlist_node *cur;
	while (memlist->pool) {
		cur = memlist->pool;
		memlist->pool = memlist->pool->next;
		free(cur->data);
		free(cur);
	}

	free(memlist);
}
