#ifndef POWAUR_ERROR_H
#define POWAUR_ERROR_H

#include "powaur.h"

const char *pw_strerror(enum _pw_errno_t err);
const char *pw_strerrorlast(void);

#endif
