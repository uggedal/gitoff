#include <sys/stat.h>
#include <sys/types.h>

#include <dirent.h>
#include <errno.h>
#include <libgen.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <git2.h>

void
eprintf(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	if (fmt[0] && fmt[strlen(fmt)-1] == ':') {
		fputc(' ', stderr);
		perror(NULL);
	}
	exit(1); /* TODO: HTML 500 response in stead? */
}

void
geprintf(const char *fmt, ...)
{
	va_list ap;
	const git_error *err;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	if ((err = giterr_last()) != NULL && err->message != NULL) {
		fputc(' ', stderr);
		puts(err->message);
	}
	exit(1); /* TODO: HTML 500 response in stead? */
}

void
printgt(const git_time_t gt)
{
	struct tm m;
	time_t t;

	t = (time_t) gt;

	if (gmtime_r(&t, &m) == NULL)
		return;

	printf("%.4u-%02u-%02u %02u:%02u",
			m.tm_year + 1900,
			m.tm_mon + 1,
			m.tm_mday,
			m.tm_hour,
			m.tm_min);
}

int
has_file(const char *base, const char *file, int isdir)
{
	struct stat st;
	char buf[PATH_MAX];

	snprintf(buf, sizeof(buf), "%s/%s", base, file);
	if (stat(buf, &st) < 0) {
		if (errno != ENOENT)
			eprintf("stat %s:", buf);
		return 0;
	}
	return isdir ? S_ISDIR(st.st_mode) : S_ISREG(st.st_mode);
}

int
valid_git_dir(const char *dir)
{
	return has_file(dir, "objects", 1) &&
		has_file(dir, "HEAD", 0) &&
		has_file(dir, "refs", 1);
}

int
walk_repos(const char *dir, int depth, int (*cb)(const char *path))
{
	int ret = 0;
	DIR *dp;
	struct dirent *d;
	struct stat st;
	char buf[PATH_MAX];

	if (depth >= 3)
		return ret;
	depth++;

	if (!(dp = opendir(dir)))
		eprintf("opendir %s:", dir);

	while ((d = readdir(dp))) {
		if (strcmp(d->d_name, ".") == 0 || strcmp(d->d_name, "..") == 0)
			continue;

		if (valid_git_dir(dir)) {
			ret |= cb(dir);
			goto cleanup;
		}

		snprintf(buf, sizeof(buf), "%s/%s", dir, d->d_name);
		if (stat(buf, &st) < 0)
			eprintf("stat %s:", buf);

		if (S_ISDIR(st.st_mode))
			walk_repos(buf, depth, cb);
	}

cleanup:
	closedir(dp);

	return ret;
}

void
http_headers()
{
	printf("Content-Type: text/html; charset=UTF-8\n\n");
	/* TODO: abort for HEAD requests */
	/* TODO: cache headers */
}

void
render_header(const char *title)
{
	printf("<!doctype html>\n"
			"<html>\n"
			"<head>\n"
			"<title>%s</title>\n"
			"<link href=/style.css rel=stylesheet>\n"
			"</head>\n"
			"<body>\n"
			"<h1>%s</h1>\n", title, title);
}

void
render_footer()
{
	puts("</body></html>");
}

int
render_repo(const char *path)
{
	char *p, *b;
	git_repository *r;
	git_object *obj;
	git_commit *ci;

	if (!(p = strdup(path)))
		eprintf("strdup:");
	if (!(b = basename(p)))
		eprintf("basename:");

	if (git_repository_open_bare(&r, path))
		geprintf("repo open %s:", path);

	if (git_revparse_single(&obj, r, "HEAD"))
		geprintf("revparse HEAD %s:", path);

	if (git_commit_lookup(&ci, r, git_object_id(obj)))
		geprintf("commit lookup %s:", git_object_id(obj));

	printf("<tr>\n"
			"<td>%s</td>\n"
			"<td>", b);
	printgt(git_commit_time(ci));
	puts("</td>\n"
			"</tr>\n");

	free(p);
	git_object_free(obj);
	git_repository_free(r);
	return 0;
}

int
render_index(void) {
	int ret = 0;

	http_headers();
	render_header("Repositories");
	puts("<table>\n"
			"<tr>\n"
			"<th>&nbsp;</th>\n"
			"<th>Latest commit</th>");
	ret = walk_repos(SCAN_DIR, 0, render_repo);
	puts("</table>");
	render_footer();
	return ret;
}

int
main(int argc, char *argv[])
{
	int ret = 0;

	ret = render_index();

	git_libgit2_init();
	git_libgit2_shutdown();

	return ret;
}
