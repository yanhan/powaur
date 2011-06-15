#include <errno.h>
#include <stdio.h>

#include <curl/curl.h>
#include <pthread.h>

#include "curl.h"
#include "download.h"
#include "error.h"
#include "environment.h"
#include "hashdb.h"
#include "powaur.h"
#include "util.h"
#include "wrapper.h"

/* For threaded downloads, points to targets */
alpm_list_t *pw_jobq = NULL;

int download_single_file(CURL *curl, const char *url, FILE *fp)
{
	int ret = 0;
	long httpresp;
	CURLcode curlret;

	curl_reset(curl);
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);

	curlret = curl_easy_perform(curl);

	if (curlret) {
		pw_fprintf(PW_LOG_ERROR, stderr, "curl: %s\n",
				   curl_easy_strerror(curlret));
		pwerrno = PW_ERR_CURL_DOWNLOAD;
		ret = -1;
	}

	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpresp);
	if (httpresp != 200) {
		pw_fprintf(PW_LOG_ERROR, stderr, "curl responded with http code: %ld\n",
				  httpresp);
		ret = -1;
	}

	if (ret) {
		pw_fprintf(PW_LOG_ERROR, stderr, "downloading %s failed.\n", url);
	}

	return ret;
}

int download_single_package(CURL *curl, const char *pkgname,
							alpm_list_t **failed_packages, int verbose)
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
			return error(PW_ERR_ACCESS, getcwd(filename, PATH_MAX));
		case EISDIR:
			return error(PW_ERR_ISDIR, getcwd(filename, PATH_MAX));
		default:
			return error(PW_ERR_ACCESS, filename);
		}

		ret = -1;
		goto cleanup;
	}

	/* Download the package */
	snprintf(url, PATH_MAX, AUR_PKGTAR_URL, pkgname, pkgname);
	ret = download_single_file(curl, url, fp);

cleanup:
	fclose(fp);

	if (!ret && verbose) {
		pw_printf(PW_LOG_INFO, "Downloaded %s\n", filename);
	}

	return ret;
}

int dl_extract_single_package(CURL *curl, const char *pkgname,
							  alpm_list_t **failed_packages, int verbose)
{
	int ret;
	char filename[PATH_MAX];

	snprintf(filename, PATH_MAX, "%s.tar.gz", pkgname);
	ret = download_single_package(curl, pkgname, failed_packages, verbose);

	if (ret) {
		unlink(filename);
		return ret;
	}

	return extract_file(filename);
}

static void *thread_dl_extract(void *useless)
{
	pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
	void *pkg;
	int ret;

	CURL *curl = curl_easy_new();
	if (!curl) {
		error(PW_ERR_CURL_INIT);
		return NULL;
	}

	while (1) {
		pthread_mutex_lock(&lock);
		pkg = alpm_list_getdata(pw_jobq);
		pw_jobq = alpm_list_next(pw_jobq);
		pthread_mutex_unlock(&lock);

		if (!pkg) {
			break;
		}

		ret = dl_extract_single_package(curl, pkg, NULL, 1);
		if (ret) {
			break;
		}
	}

	curl_easy_cleanup(curl);
	return NULL;
}

static void threadpool_dl_extract(alpm_list_t *targets)
{
	pthread_attr_t attr;
	pthread_t *threads;

	int i, ret, num_threads;
	void *placeholder;

	num_threads = alpm_list_count(targets);
	num_threads = num_threads > config->maxthreads ? config->maxthreads : num_threads;

	pw_jobq = targets;

	threads = xcalloc(num_threads, sizeof(pthread_t));
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

	pw_printf(PW_LOG_DEBUG, "Spawning %d threads.\n", num_threads);
	for (i = 0; i < num_threads; ++i) {
		ret = pthread_create(&threads[i], &attr, thread_dl_extract, placeholder);
		if (ret) {
			die_errno(PW_ERR_PTHREAD_CREATE);
		}

		pw_printf(PW_LOG_DEBUG, "Created thread %d\n", i);
	}

	pthread_attr_destroy(&attr);

	for (i = 0; i < num_threads; ++i) {
		ret = pthread_join(threads[i], &placeholder);
		if (ret) {
			die_errno(PW_ERR_PTHREAD_JOIN);
		}

		pw_printf(PW_LOG_DEBUG, "%d threads joined.\n", i+1);
	}

	free(threads);
}

/* Download pkgbuilds and extract in current directory.
 */
int powaur_get(alpm_list_t *targets)
{
	int errors;
	alpm_list_t *i, *failed_packages;
	alpm_list_t *resolve, *new_resolve;
	char filename[PATH_MAX];
	char dirpath[PATH_MAX];
	CURL *curl;

	errors = 0;
	failed_packages = resolve = new_resolve = NULL;

	if (!targets) {
		return error(PW_ERR_TARGETS_NULL, "-G");
	}

	/* Check for --target */
	if (config->target_dir) {
		if (!realpath(config->target_dir, dirpath)) {
			return error(PW_ERR_PATH_RESOLVE, config->target_dir);
		}

		if (chdir(dirpath)) {
			return error(PW_ERR_CHDIR, dirpath);
		}
	} else {
		getcwd(dirpath, PATH_MAX);
	}

	pw_printf(PW_LOG_INFO, "Downloading files to %s\n", dirpath);

	curl = curl_easy_new();
	if (!curl) {
		errors = 1;
		goto cleanup;
	}

	if (!config->op_g_resolve) {
		threadpool_dl_extract(targets);
		goto cleanup;
	}

	/* Threaded downloading w/ dependency resolution */
	struct pw_hashdb *hashdb = build_hashdb();
	if (!hashdb) {
		pw_fprintf(PW_LOG_ERROR, stderr, "Failed to build hash database!\n");
		errors++;
		goto cleanup;
	}
	resolve = alpm_list_strdup(targets);

	while (resolve) {
		threadpool_dl_extract(resolve);
		new_resolve = resolve_dependencies(hashdb, resolve);
		FREELIST(resolve);
		resolve = new_resolve;

		if (config->loglvl & PW_LOG_DEBUG) {
			pw_printf(PW_LOG_DEBUG, "Downloading the following deps:\n");
			for (i = resolve; i; i = i->next) {
				pw_printf(PW_LOG_DEBUG, "%s%s\n", TAB, i->data);
			}
			pw_printf(PW_LOG_DEBUG, "\n");
		}
	}

	hashdb_free(hashdb);

cleanup:
	alpm_list_free(failed_packages);
	curl_easy_cleanup(curl);
	return errors ? -1 : 0;
}
