#ifndef PW_DOWNLOAD_H
#define PW_DOWNLOAD_H

#include <alpm_list.h>

int download_single_file(const char *url, FILE *fp);
int download_single_package(const char *pkgname, alpm_list_t **failed_packages);
int download_packages(alpm_list_t *packages, alpm_list_t **failed_packages);

int powaur_get(alpm_list_t *targets);

#endif
