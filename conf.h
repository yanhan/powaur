#ifndef POWAUR_CONF_H
#define POWAUR_CONF_H

#include <stdio.h>

#include "powaur.h"

struct config_t {
	unsigned short op;
	enum pwloglevel_t loglvl;

	unsigned help: 1;
	unsigned version: 1;

	/* Query options */
	unsigned op_q_info: 1;
	unsigned op_q_search: 1;

	/* Sync options */
	unsigned op_s_info: 1;
	unsigned op_s_search: 1;
	unsigned op_s_detailed: 1;	/* TODO: Remove this */

	/* Misc */
	unsigned sort_votes: 1;
	unsigned verbose: 1;
};

struct config_t *config_init(void);
void config_free(struct config_t *conf);

int parse_powaur_config(FILE *fp);

/* Briefly parse /etc/pacman.conf */
int parse_pmconfig(void);

#endif
