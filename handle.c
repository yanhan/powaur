#include <stdio.h>
#include <sys/stat.h>

#include "handle.h"
#include "json.h"
#include "util.h"

struct pwhandle_t *pwhandle = NULL;

struct pwhandle_t *_pwhandle_init(void)
{
	struct pwhandle_t *hand;
	hand = calloc(1, sizeof(struct pwhandle_t));
	ASSERT(hand != NULL, RET_ERR(PW_ERR_MEMORY, NULL));

	hand->json_ctx = calloc(1, sizeof(struct json_ctx_t));
	ASSERT(hand->json_ctx != NULL, free(hand);
		   RET_ERR(PW_ERR_MEMORY, NULL));

	return hand;
}

void _pwhandle_free(struct pwhandle_t *hand)
{
	if (hand) {
		FREE(hand->json_ctx);
		FREE(hand);
	}
}
