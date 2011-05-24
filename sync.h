#ifndef POWAUR_SYNC_H
#define POWAUR_SYNC_H

#include <alpm_list.h>

int powaur_sync(alpm_list_t *targets);
int powaur_maint(alpm_list_t *targets);

#endif
