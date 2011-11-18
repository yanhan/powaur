#ifndef POWAUR_ARGV_ARRAY_H
#define POWAUR_ARGV_ARRAY_H

struct argv_array {
	char **argv;
	int nr;
	int alloc;
};

#define ARGV_ARRAY_INIT {0, 0, 0}

void argv_array_push(struct argv_array *argv_array, char *arg);
void argv_array_free(struct argv_array *argv_array, int free);

#endif
