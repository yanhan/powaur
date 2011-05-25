#include <stdio.h>
#include <sys/stat.h>

#include "handle.h"
#include "json.h"
#include "wrapper.h"

struct pwhandle_t *pwhandle = NULL;

struct pwhandle_t *_pwhandle_init(void)
{
	struct pwhandle_t *hand;
	hand = xcalloc(1, sizeof(struct pwhandle_t));
	hand->json_ctx = xcalloc(1, sizeof(struct json_ctx_t));
	return hand;
}

void _pwhandle_free(struct pwhandle_t *hand)
{
	if (hand) {
		free(hand->json_ctx);
		free(hand);
	}
}
