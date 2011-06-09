#ifndef PW_DOWNLOAD_H
#define PW_DOWNLOAD_H

#include <alpm_list.h>
#include <curl/curl.h>

int download_single_file(CURL *curl, const char *url, FILE *fp);

/* Downloads a single tarball from AUR.
 * Assumption: We are already in destination directory.
 *
 * returns 0 on success, -1 on failure.
 *
 * @param curl curl handle
 * @param pkgname package name
 * @param failed_packages pointer to list to store packages which fail to dl
 * @param verbose show download messages if non-zero
 */
int download_single_package(CURL *curl, const char *pkgname,
							alpm_list_t **failed_packages, int verbose);

int download_packages(CURL *curl, alpm_list_t *packages, alpm_list_t **failed_packages);

/* Downloads and extracts a single package.
 * returns 0 on success, -1 on failure.
 *
 * @param curl curl handle
 * @param pkgname package name
 * @param failed_packages pointer to list to store packages which fail to dl/extract
 * @param verbose show download messages if non-zero
 */
int dl_extract_single_package(CURL *curl, const char *pkgname,
							  alpm_list_t **failed_packages, int verbose);

int powaur_get(alpm_list_t *targets);

#endif
