#include <errno.h>
#include <stdio.h>

#include "curl.h"
#include "download.h"
#include "environment.h"
#include "util.h"

/* Assumption: curl options have been set except for url and writedata
 * returns -1 on failure, 0 on success.
 */
int download_single_file(const char *url, FILE *fp)
{
	int ret = 0;
	long httpresp;
	CURLcode curlret;

	/* Must do this here since this function is shared */
	curl_init();
	ASSERT(curl != NULL, RET_ERR(PW_ERR_CURL_INIT, -1));

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);

	curlret = curl_easy_perform(curl);

	if (curlret) {
		pw_fprintf(PW_LOG_ERROR, stderr, "curl: %s\n",
				   curl_easy_strerror(curlret));
		pwerrno = PW_ERR_CURL_DOWNLOAD;
		pw_fprintf(PW_LOG_ERROR, stderr, "downloading %s failed.\n", url);
		ret = -1;
	}

	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpresp);
	if (httpresp != 200) {
		pw_fprintf(PW_LOG_ERROR, stderr, "curl responded with http code: %ld\n",
				  httpresp);
		pw_fprintf(PW_LOG_ERROR, stderr, "downloading %s failed.\n", url);
		ret = -1;
	}

	return ret;
}

/* Downloads a single tarball from AUR.
 * Assumption: We're already in destination dir.
 *
 * returns 0 on success, -1 on failure. 
 * Failed package is added to failed_packages list.
 */
int download_single_package(const char *pkgname, alpm_list_t **failed_packages)
{
	int ret = 0;
	FILE *fp = NULL;

	char filename[PATH_MAX];
	char url[PATH_MAX];

	snprintf(filename, PATH_MAX, "%s.tar.gz", pkgname);
	fp = fopen(filename, "w");
	if (!fp) {
		if (failed_packages) {
			*failed_packages = alpm_list_add(*failed_packages, (void *) pkgname);
		}

		switch (errno) {
		case EACCES:
			PW_SETERRNO(PW_ERR_ACCESS);
			break;
		case EISDIR:
			PW_SETERRNO(PW_ERR_ISDIR);
			break;
		default:
			PW_SETERRNO(PW_ERR_FOPEN);
		}

		ret = -1;
		goto cleanup;
	}

	/* Download the package */
	snprintf(url, PATH_MAX, AUR_PKGTAR_URL, pkgname, pkgname);
	ret = download_single_file(url, fp);

cleanup:
	FCLOSE(fp);
	return ret;
}

/* Downloads all the tarball PKGBUILDS from AUR.
 * returns 0 on success, n >= 1 where n is no. of failed packages.
 */
int download_packages(alpm_list_t *packages, alpm_list_t **failed_packages)
{
	int errors = 0;
	int ret = 0;
	alpm_list_t *i;

	for (i = packages; i; i = i->next) {
		ret = download_single_package((char *) i->data, failed_packages);
		errors += ret ? 1:
				  printf("Successfully downloaded %s.tar.gz\n", i->data) && 0;
	}

	return errors;
}

/* Downloads and extracts a single package */
int dl_extract_single_package(const char *pkgname, alpm_list_t **failed_packages)
{
	int ret;
	char filename[PATH_MAX];

	snprintf(filename, PATH_MAX, "%s.tar.gz", pkgname);
	ret = download_single_package(pkgname, failed_packages);

	if (ret) {
		unlink(filename);
		return ret;
	}

	return extract_file(filename, 1);
}

/* Download pkgbuilds and extract in current directory.
 */
int powaur_get(alpm_list_t *targets)
{
	ASSERT(targets != NULL, RET_ERR(PW_ERR_TARGETS_NULL, -1));

	curl_init();
	ASSERT(curl != NULL, RET_ERR(PW_ERR_CURL_INIT, -1));

	int errors = 0;
	alpm_list_t *i, *failed_packages;
	alpm_list_t *resolve, *new_resolve;
	char filename[PATH_MAX];

	failed_packages = resolve = new_resolve = NULL;

	for (i = targets; i; i = i->next) {
		pwerrno = 0;
		errors += download_single_package(i->data, &failed_packages);

		if (pwerrno == 0) {
			pw_printf(PW_LOG_INFO, "Downloaded %s.tar.gz\n", i->data);
			snprintf(filename, PATH_MAX, "%s.tar.gz", i->data);
			errors += extract_file(filename, 1) ? 1 : 0;
		} else if (pwerrno == PW_ERR_ACCESS) {
			/* No write permission */
			break;
		}

		resolve = alpm_list_add(resolve, strdup(i->data));
	}

	/* Resolve dependencies */
	while (resolve) {
		new_resolve = resolve_dependencies(resolve);
		FREELIST(resolve);
		resolve = new_resolve;

		for (i = resolve; i; i = i->next) {
			if (dl_extract_single_package(i->data, &failed_packages)) {
				pw_fprintf(PW_LOG_ERROR, stderr, "Could not download \"%s\""
						   " from the AUR\n", i->data);
				++errors;
			}
		}
	}

cleanup:
	alpm_list_free(failed_packages);
	return errors ? -1 : 0;
}
