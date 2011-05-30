#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <archive.h>
#include <archive_entry.h>

#include "config.h"
#include "environment.h"
#include "package.h"
#include "powaur.h"
#include "util.h"

int pw_printf(enum pwloglevel_t lvl, const char *fmt, ...)
{
	int ret;
	va_list ap;

	va_start(ap, fmt);
	ret = pw_vfprintf(lvl, stdout, fmt, ap);
	va_end(ap);

	return ret;
}

int pw_fprintf(enum pwloglevel_t lvl, FILE *stream, const char *fmt, ...)
{
	int ret;
	va_list ap;

	va_start(ap, fmt);
	ret = pw_vfprintf(lvl, stderr, fmt, ap);
	va_end(ap);

	return ret;
}

int pw_vfprintf(enum pwloglevel_t lvl, FILE *stream, const char *fmt, va_list ap)
{
	int ret = 0;
	va_list copy_list;

	if (!config || !(config->loglvl & lvl)) {
		return ret;
	}

	va_copy(copy_list, ap);

	switch (lvl) {
	case PW_LOG_WARNING:
		fprintf(stream, "WARNING: ");
		break;
	case PW_LOG_ERROR:
		fprintf(stream, "error: ");
		break;
	case PW_LOG_INFO:
		fprintf(stream, "==> ");
		break;
	case PW_LOG_DEBUG:
		fprintf(stream, "debug: ");
		break;
	}

	ret = vfprintf(stream, fmt, ap);
	return ret;
}

/* Extracts the downloaded archive and removes it upon success.
 * Assumed to be in destination directory before calling this.
 * Returns -1 on fatal errors, > 0 on extraction errors, 0 on success.
 */
int extract_file(const char *filename)
{
	/* Extract the archive */
	struct archive *archive;
	struct archive_entry *entry;
	int ret;
	int errors = 0;
	int extract_flags = ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_TIME;

	archive = archive_read_new();
	if (!archive) {
		return error(PW_ERR_ARCHIVE_CREATE);
	}

	archive_read_support_compression_all(archive);
	archive_read_support_format_all(archive);
	ret = archive_read_open_filename(archive, filename,
									 ARCHIVE_DEFAULT_BYTES_PER_BLOCK);

	if (ret != ARCHIVE_OK) {
		return error(PW_ERR_ARCHIVE_OPEN);
	}

	while (archive_read_next_header(archive, &entry) == ARCHIVE_OK) {
		ret = archive_read_extract(archive, entry, extract_flags);

		if (ret == ARCHIVE_WARN && archive_errno(archive) != ENOSPC) {
			pw_fprintf(PW_LOG_WARNING, stderr,
					   "warning given when extracting %s: %s\n",
					   archive_entry_pathname(entry),
					   archive_error_string(archive));

		} else if (ret != ARCHIVE_OK) {
			pw_fprintf(PW_LOG_ERROR, stderr, "Could not extract %s\n",
					   archive_entry_pathname(entry));
			++errors;
		}

		if (config->verbose) {
			printf("X %s\n", archive_entry_pathname(entry));
		}
	}

	archive_read_finish(archive);

	/* Everything successful. Remove the file */
	unlink(filename);
	return errors;
}

/* Removes whitespace from both ends of a string */
char *strtrim(char *line)
{
	while (*line && isspace(*line)) {
		++line;
	}

	if (!*line) {
		return line;
	}

	int len = strlen(line);
	char *ptr;

	ptr = line + len - 1;
	while (ptr >= line && isspace(*ptr)) {
		*ptr-- = 0;
	}

	return line;
}

/* From pacman */
int getcols(void)
{
#ifdef TIOCGSIZE
	struct ttysize w;
	if (ioctl(STDOUT_FILENO, TIOCGSIZE, &w) == 0) {
		return w.ts_cols;
	}
#elif defined(TIOCGWINSZ)
	struct winsize w;
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
		return w.ws_col;
	}
#endif
	return 0;
}

/* From git */
int wait_or_whine(pid_t pid, char *argv0)
{
	int status, ret = 0;
	pid_t waiting;

	while ((waiting = waitpid(pid, &status, 0)) < 0 && errno == EINTR)
		;

	if (waiting < 0) {
		return error(PW_ERR_WAITPID_FAILED);
	} else if (waiting != pid) {
		return error(PW_ERR_WAITPID_CONFUSED);
	} else if (WIFSIGNALED(status)) {
		ret = WTERMSIG(status);
		ret -= 127;
		RET_ERR(PW_ERR_WAITPID_SIGNAL, ret);
	} else if (WIFEXITED(status)) {
		ret = WEXITSTATUS(status);

		if (ret == 127) {
			ret = -1;
			pw_fprintf(PW_LOG_ERROR, stderr, "Failed to execute %s\n",
					   argv0);
		}

	} else {
		return error(PW_ERR_WAITPID_CONFUSED);
	}

	return ret;
}

/* Prints list of char *, one on each line with proper indentation */
void indent_print(enum pwloglevel_t lvl, alpm_list_t *list, size_t indent)
{
	alpm_list_t *i;
	int k;
	size_t prefix_len = 0;

	switch (lvl) {
	case PW_LOG_NORM:
		prefix_len = 0;
		break;
	case PW_LOG_INFO:
		prefix_len = 4;
		break;
	case PW_LOG_WARNING:
		prefix_len = 9;
		break;
	case PW_LOG_ERROR:
		prefix_len = 7;
		break;
	case PW_LOG_DEBUG:
		prefix_len = 7;
		break;
	}

	indent += prefix_len;
	for (i = list; i; i = i->next) {
		pw_printf(lvl, "%*s%s\n", indent, "", i->data);
	}
}

void print_list(alpm_list_t *list, const char *prefix)
{
	alpm_list_t *i;
	size_t len = strlen(prefix);
	size_t indent, curcols, newlen;

	size_t cols = getcols();
	printf("%s%s%s ", color.bold, prefix, color.nocolor);

	if (!list) {
		printf("None\n");
		return;
	}

	indent = len + 1;
	curcols = indent;
	for (i = list; i; i = i->next) {
		/* There's a bug here which I'm not gonna fix. */
		newlen = curcols + 2 + strlen(i->data);
		if (newlen > cols) {
			printf("\n");
			printf("%*s", indent, "");
			curcols = indent;
		}

		if (curcols == indent) {
			printf("%s", i->data);
			curcols += strlen(i->data);
			continue;
		}

		curcols = newlen;
		printf("  %s", i->data);
	}

	printf("\n");
}

/* Print dependencies */
void print_list_deps(alpm_list_t *list, const char *prefix)
{
	alpm_list_t *i;
	size_t indent, cols, curcols, newlen;
	size_t depstrlen;
	char *depstr;

	printf("%s%s%s ", color.bold, prefix, color.nocolor);
	if (!list) {
		printf("None\n");
		return;
	}

	cols = getcols();
	indent = strlen(prefix) + 1;
	curcols = indent;

	for (i = list; i; i = i->next) {
		/* Compute dep string */
		depstr = alpm_dep_compute_string(i->data);
		if (!depstr) {
			continue;
		}

		depstrlen = strlen(depstr);
		newlen = curcols + 2 + depstrlen;

		if (newlen > cols) {
			printf("\n");
			printf("%*s", indent, "");
			curcols = indent;
		}

		if (curcols == indent) {
			printf("%s", depstr);
			curcols += depstrlen;
		} else {
			printf("  %s", depstr);
			curcols += 2 + depstrlen;
		}

		free(depstr);
	}

	printf("\n");
}

void print_list_break(alpm_list_t *list, const char *prefix)
{
	alpm_list_t *i;
	size_t indent;

	printf("%s%s%s ", color.bold, prefix, color.nocolor);
	if (!list) {
		printf("None\n");
		return;
	}

	printf("%s\n", list->data);

	indent = strlen(prefix) + 1;
	for (i = list->next; i; i = i->next) {
		printf("%*s%s\n", indent, "", i->data);
	}
}

/* Question which requires a y/n answer.
 * returns 0 for n, 1 for y
 */
static int question(int preset, const char *fmt, va_list args)
{
	int ret, len;
	char response[MINI_BUFSZ];
	char *str;

	fflush(stdout);
	do {
		vprintf(fmt, args);
		if (preset) {
			printf(" (Y/n) ");
		} else {
			printf(" (y/N) ");
		}

		str = fgets(response, sizeof(response), stdin);
		str = strtrim(str);
		len = strlen(str);

		if (!len) {
			return preset;
		} else {
			if (!strcasecmp(str, "n") || !strcasecmp(str, "no")) {
				return 0;
			} else if (!strcasecmp(str, "y") || !strcasecmp(str, "yes")) {
				return 1;
			}
		}

	} while (1);
}

/* Prints list of struct pkginfo_t * */
void print_pkginfo(alpm_list_t *list)
{
	alpm_list_t *i;
	size_t cols, curcols, newlen;
	size_t namelen, verlen;
	struct pkginfo_t *pkginfo;

	cols = getcols();
	curcols = 0;

	for (i = list; i; i = i->next) {
		pkginfo = i->data;
		namelen = strlen(pkginfo->name);
		verlen = strlen(pkginfo->version);

		newlen = curcols + 2 + namelen + verlen;
		if (newlen > cols) {
			printf("\n");
			curcols = 0;
		}

		if (curcols == 0) {
			printf("%s %s", pkginfo->name, pkginfo->version);
			curcols = namelen + verlen;
		} else {
			printf("  %s %s", pkginfo->name, pkginfo->version);
			curcols = newlen;
		}
	}

	printf("\n");
}

/* From pacman */
int yesno(const char *fmt, ...)
{
	int ret;
	va_list args;
	va_start(args, fmt);

	ret = question(1, fmt, args);

	va_end(args);
	return ret;
}

/* Returns 1 if invalid, 0 if valid */
static int check_mcq_response(const void *resp, size_t sz, const void *choices,
							  int numChoices)
{
	int i;
	for (i = 0; i < numChoices; ++i) {
		if (!memcmp(resp, (char *) choices + i * sz, sz)) {
			return i;
		}
	}

	return -1;
}

int mcq(const char *qn, const char *choices, int arraySize, int preset)
{
	int i, len, ans;
	int invalid = 1;
	char response[MINI_BUFSZ];
	char *str;

	fflush(stdout);

	do {
		printf("%s ", qn);
		str = fgets(response, MINI_BUFSZ, stdin);

		str = strtrim(str);
		len = strlen(str);
		if (len == 0) {
			return preset;
		}

		/* Get response */
		ans = check_mcq_response(str, sizeof(char), choices, arraySize);
	} while (ans == -1);

	return ans;
}

/* Checks if we have .install file in PKGBUILD at cwd.
 * returns a newly allocated string for the .install file if we have
 * returns NULL if no
 */
char *have_dotinstall(void)
{
	FILE *fp;
	char buf[PATH_MAX];
	char *str, *dotinstall = NULL;
	size_t len;

	fp = fopen("PKGBUILD", "r");
	if (!fp) {
		error(PW_ERR_FOPEN, "PKGBUILD");
		return NULL;
	}

	while (str = fgets(buf, PATH_MAX, fp)) {
		str = strtrim(str);
		len = strlen(str);
		if (!len) {
			continue;
		}

		if (!strncmp(str, "install", 7)) {
			/* No error checking done here, if malformed, that's it */
			str = strchr(str, '=');
			if (!str) {
				break;
			}

			++str;
			str = strtrim(str);
			if (!strlen(str)) {
				break;
			} else {
				dotinstall = strdup(str);
				break;
			}
		}
	}

	fclose(fp);
	return dotinstall;
}

/* Unsorted list diff, O(n^2) */
alpm_list_t *list_diff(alpm_list_t *left, alpm_list_t *right,
					   alpm_list_fn_cmp cmpfn)
{
	alpm_list_t *i, *j;
	alpm_list_t *ret = NULL;
	int add_to_list;

	for (i = left; i; i = i->next) {
		add_to_list = 1;
		for (j = right; j; j = j->next) {
			if (!cmpfn(i->data, j->data)) {
				add_to_list = 0;
				break;
			}
		}

		if (add_to_list) {
			ret = alpm_list_add(ret, i->data);
		}
	}

	return ret;
}

/* Intersection of left and right lists.
 * returns a new list of data taken from left list for intersections
 */
alpm_list_t *list_intersect(alpm_list_t *left, alpm_list_t *right,
							alpm_list_fn_cmp cmpfn)
{
	alpm_list_t *intersect = NULL;
	alpm_list_t *i, *k;
	int add_to_list;

	for (i = left; i; i = i->next) {
		add_to_list = 0;
		for (k = right; k; k = k->next) {
			if (!cmpfn(i->data, k->data)) {
				add_to_list = 1;
				break;
			}
		}

		if (add_to_list) {
			intersect = alpm_list_add(intersect, i->data);
		}
	}

	return intersect;
}

void color_repo(const char *repo)
{
	if (!strcmp(repo, "core")) {
		printf("%s", color.bred);
	} else if (!strcmp(repo, "extra")) {
		printf("%s", color.bgreen);
	} else if (!strcmp(repo, "local")) {
		printf("%s", color.byellow);
	} else {
		printf("%s", color.bmag);
	}

	printf("%s/%s", repo, color.nocolor);
}

void color_groups(alpm_list_t *grp)
{
	if (!grp) {
		printf("%s\n", color.nocolor);
		return;
	}

	alpm_list_t *i;
	int cnt = 0;

	printf(" %s(", color.bblue);
	for (i = grp; i; i = i->next) {
		if (cnt++) {
			printf(" ");
		}

		printf("%s", i->data);
	}
	printf(")%s\n", color.nocolor);
}

/* Writes a directory to an archive */
static int write_dir_archive(char *dirname, struct archive *a)
{
	int ret = 0;

	struct archive_entry *entry;
	struct dirent *dir_entry;
	DIR *dirp;
	struct stat st;

	char filename[PATH_MAX];
	char buf[PATH_MAX];
	int fd;
	ssize_t bytesread;

	dirp = opendir(dirname);
	if (!dirp) {
		return error(PW_ERR_OPENDIR);
	}

	while (dir_entry = readdir(dirp)) {
		if (!strcmp(dir_entry->d_name, ".") || !strcmp(dir_entry->d_name, "..")) {
			continue;
		}

		snprintf(filename, PATH_MAX, "%s/%s", dirname, dir_entry->d_name);
		if (stat(filename, &st)) {
			pw_fprintf(PW_LOG_ERROR, stderr, "%s: Failed to stat file %s\n",
					   __func__, filename);
			ret = -1;
			goto free_entry;
		}

		entry = archive_entry_new();
		if (!entry) {
			pw_fprintf(PW_LOG_ERROR, stderr, "%s: Failed to create new entry\n",
					   __func__);
			ret = -1;
			goto free_entry;
		}

		archive_entry_set_pathname(entry, filename);
		archive_entry_copy_stat(entry, &st);
		archive_write_header(a, entry);

		if (st.st_mode & S_IFDIR) {
			/* Directory entry. NOTE: Recursion
			 * I don't really like recursion but there doesn't seem to be
			 * a more elegant way out.
			 */
			snprintf(filename, PATH_MAX, "%s/%s", dirname, dir_entry->d_name);
			if (ret = write_dir_archive(filename, a)) {
				goto free_entry;
			}

		} else {
			fd = open(filename, O_RDONLY);
			if (fd < 0) {
				pw_fprintf(PW_LOG_ERROR, stderr, "Cannot open %s\n", filename);
				goto free_entry;
			}

			while (bytesread = read(fd, buf, PATH_MAX)) {
				archive_write_data(a, buf, bytesread);
			}

			close(fd);
		}

free_entry:
		archive_entry_free(entry);
		entry = NULL;
	}

	closedir(dirp);
	return ret;
}

int powaur_backup(alpm_list_t *targets)
{
	int ret = 0;
	char localdb[PATH_MAX];
	struct archive *a;
	struct archive_entry *entry;
	struct stat st;

	char cwd[PATH_MAX];
	char backup_dest[PATH_MAX];
	char backup[MINI_BUFSZ];

	time_t time_now;
	struct tm tm_st;

	if (targets != NULL && alpm_list_count(targets) != 1) {
		pw_fprintf(PW_LOG_ERROR, stderr, "-B only takes 1 argument.\n");
		return -1;
	}

	a = archive_write_new();
	if (!a) {
		return error(PW_ERR_ARCHIVE_CREATE);
	}

	archive_write_set_compression_bzip2(a);
	archive_write_set_format_pax_restricted(a);

	/* Filename = pacman-YYYY-MM-DD_HHhMM.tar.bz2 */
	time(&time_now);
	localtime_r(&time_now, &tm_st);
	strftime(backup, MINI_BUFSZ, "pacman-%Y-%m-%d_%Hh%M.tar.bz2", &tm_st);

	/* Get full path */
	if (targets) {
		snprintf(backup_dest, PATH_MAX, "%s/%s", targets->data, backup);
	} else {
		snprintf(backup_dest, PATH_MAX, "%s", backup);
	}

	if (archive_write_open_filename(a, backup_dest) != ARCHIVE_OK) {
		PW_SETERRNO(PW_ERR_ARCHIVE_OPEN);
		ret = -1;
		goto cleanup;
	}

	if (!getcwd(cwd, PATH_MAX)) {
		error(PW_ERR_GETCWD);
		ret = -1;
		goto cleanup;
	}

	if (ret = chdir(pacman_dbpath)) {
		error(PW_ERR_CHDIR, pacman_dbpath);
		goto restore_cwd;
	}

	/* Create entry for the current directory. */
	entry = archive_entry_new();
	if (!entry) {
		error(PW_ERR_ARCHIVE_ENTRY);
		goto restore_cwd;
	}

	snprintf(localdb, PATH_MAX, "%s", "local");
	if (ret = stat(localdb, &st)) {
		error(PW_ERR_STAT, localdb);
		goto free_entry;
	}

	archive_entry_set_pathname(entry, localdb);
	archive_entry_copy_stat(entry, &st);
	archive_write_header(a, entry);

	pw_printf(PW_LOG_INFO, "Saving pacman database in %s\n", backup_dest);

	ret = write_dir_archive(localdb, a);

	if (!ret) {
		pw_printf(PW_LOG_INFO, "Pacman database successfully saved in %s\n",
				  backup_dest);
	} else {
		pw_fprintf(PW_LOG_ERROR, stderr, "Pacman database not saved.\n");
	}

free_entry:
	archive_entry_free(entry);

restore_cwd:
	if (chdir(cwd)) {
		PW_SETERRNO(PW_ERR_RESTORECWD);
		ret = -1;
	}

cleanup:
	archive_write_finish(a);
	return ret;
}
