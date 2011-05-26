#ifndef POWAUR_ERROR_H
#define POWAUR_ERROR_H

#include "powaur.h"

int error(enum _pw_errno_t err, ...);
int error_report(const char *msg, ...);
void die(const char *msg, ...);
void die_errno(enum _pw_errno_t err, ...);

const char *pw_strerror(enum _pw_errno_t err);
const char *pw_strerrorlast(void);

#endif
