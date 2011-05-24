#ifndef POWAUR_HANDLE_H
#define POWAUR_HANDLE_H

#include <stdio.h>

#include "json.h"

struct pwhandle_t {
	struct json_ctx_t *json_ctx;
};

extern struct pwhandle_t *pwhandle;

struct pwhandle_t *_pwhandle_init(void);
void _pwhandle_free(struct pwhandle_t *);

#endif
