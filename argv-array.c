#include "wrapper.h"
#include "argv-array.h"

#define alloc_nr(x) (((x)+16)*3/2)

static void argv_array_grow(struct argv_array *argv_array)
{
	argv_array->alloc = alloc_nr(argv_array->alloc);
	argv_array->argv = xrealloc(argv_array->argv,
							argv_array->alloc * sizeof(char *));
}

void argv_array_push(struct argv_array *argv_array, char *arg)
{
	if (argv_array->nr + 1 >= argv_array->alloc)
		argv_array_grow(argv_array);

	argv_array->argv[argv_array->nr++] = arg;
	argv_array->argv[argv_array->nr] = NULL;
}

void argv_array_free(struct argv_array *argv_array, int free_inner)
{
	if (argv_array->argv) {
		if (free_inner) {
			int i;
			for (i = 0; i < argv_array->nr; i++)
				free(argv_array->argv[i]);
		}
		free(argv_array->argv);
	}

	argv_array->argv = NULL;
	argv_array->nr = argv_array->alloc = 0;
}

void argv_array_copy(struct argv_array *dest, struct argv_array *src)
{
	int i;

	dest->nr = src->nr;
	dest->alloc = src->alloc;
	if (dest->alloc > 0) {
		dest->argv = xcalloc(dest->alloc, sizeof(char *));
		for (i = 0; i < dest->nr; i++)
			dest->argv[i] = xstrdup(src->argv[i]);
		dest->argv[dest->nr] = NULL;
	}
}
