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

#define REPO_NAME_MAX 64

struct repo {
	char path[PATH_MAX];
	char name[REPO_NAME_MAX];
	git_time_t age;
	git_repository *handle;
};

struct repos {
	size_t n;
	struct repo *repos;
};

void
eprintf(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	if (fmt[0] && fmt[strlen(fmt) - 1] == ':') {
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
	} else
		fputc('\n', stderr);
	exit(1); /* TODO: HTML 500 response in stead? */
}

void
printgt(const git_time_t gt)
{
	struct tm m;

	if (gmtime_r(&gt, &m) == NULL)
		return;

	printf("%.4u-%02u-%02u %02u:%02u",
	    m.tm_year + 1900,
	    m.tm_mon + 1,
	    m.tm_mday,
	    m.tm_hour,
	    m.tm_min);
}

static int
has_file(const char *base, const char *file, int isdir)
{
	char buf[PATH_MAX];
	struct stat st;

	snprintf(buf, sizeof(buf), "%s/%s", base, file);
	if (stat(buf, &st) < 0) {
		if (errno != ENOENT)
			eprintf("stat %s:", buf);
		return 0;
	}
	return isdir ? S_ISDIR(st.st_mode) : S_ISREG(st.st_mode);
}

static int
valid_git_dir(const char *dir)
{
	return has_file(dir, "objects", 1) &&
	    has_file(dir, "HEAD", 0) &&
	    has_file(dir, "refs", 1);
}

static void
set_repo_name(const struct repo *rp) {
	const char *p;

	if (strncmp(rp->path, SCAN_DIR"/", strlen(SCAN_DIR"/")) != 0)
		eprintf("repo path not subdir of SCAN_DIR\n");

	p = rp->path + strlen(SCAN_DIR"/");
	strlcpy((char *)rp->name, p, sizeof(rp->name));
}

static void
find_repos(struct repos *rsp, const char *dir, int depth)
{
	DIR *dp;
	struct dirent *d;
	char buf[PATH_MAX];
	struct repo *rp;

	if (depth >= 3)
		return;
	depth++;

	if (!(dp = opendir(dir)))
		eprintf("opendir %s:", dir);

	while ((d = readdir(dp))) {
		if (strcmp(d->d_name, ".") == 0 ||
		    strcmp(d->d_name, "..") == 0)
			continue;

		if (valid_git_dir(dir)) {
			rsp->repos = reallocarray(rsp->repos, ++rsp->n,
			    sizeof(struct repo));
			if (rsp->repos == NULL)
				eprintf("reallocarray:");
			rp = &rsp->repos[rsp->n - 1];
			strlcpy(rp->path, dir, PATH_MAX);
			set_repo_name(rp);
			break;
		}

		snprintf(buf, sizeof(buf), "%s/%s", dir, d->d_name);
		if (has_file(dir, d->d_name, 1))
			find_repos(rsp, buf, depth);
	}

	closedir(dp);
}

static void
parse_repo(struct repo *rp)
{
	git_repository *r;
	git_reference *ref;
	git_commit *ci;

	if (git_repository_open_bare(&r, rp->path))
		geprintf("repo open %s:", rp->path);
	if (git_repository_head(&ref, r))
		geprintf("repository_head %s:", rp->path);
	if (git_commit_lookup(&ci, r, git_reference_target(ref)))
		geprintf("commit lookup %s:", rp->path);

	rp->age = git_commit_time(ci);
	rp->handle = r;

	git_reference_free(ref);
}

static void
parse_repos(const struct repos *rsp)
{
	size_t i;

	for (i = 0; i < rsp->n; i++) {
		parse_repo(&rsp->repos[i]);
		git_repository_free(rsp->repos[i].handle);
	}
}

static void
http_headers(const char *status)
{
	printf("Content-Type: text/html; charset=UTF-8\n"
	    "Status: %s\n\n", status);
	/* TODO: abort for HEAD requests */
	/* TODO: cache headers */
}

static void
render_header(const char *title, const char *heading)
{
	printf("<!doctype html>\n"
	    "<html>\n"
	    "<head>\n"
	    "<title>%s</title>\n"
	    "<link href=/gitoff.css rel=stylesheet>\n"
	    "</head>\n"
	    "<body>\n"
	    "<h1>%s</h1>\n",
	    title, heading ? heading : title);
}

static void
render_footer()
{
	puts("</body></html>");
}

static int
repocmp(const void *va, const void *vb)
{
	const struct repo *a = va, *b = vb;
	return b->age - a->age;
}

static void
render_notfound(void)
{
	http_headers("404 Not Found");
	render_header("404 Not Found", NULL);
	render_footer();
}

static void
render_index_line(const struct repo *rp)
{
	puts("<tr>\n"
	    "<td>");
	printgt(rp->age);
	printf("</td>\n"
	    "<td>\n"
	    "<a href=/%s>%s</a>",
	    rp->name, rp->name);
	puts("</td>\n"
	    "</tr>");
}

static void
render_index(const struct repos *rsp)
{
	size_t i;

	parse_repos(rsp);
	qsort(rsp->repos, rsp->n, sizeof(*rsp->repos), repocmp);

	http_headers("200 Success");
	render_header("Repositories", NULL);
	if (rsp->n > 0) {
		puts("<table>\n"
		    "<tr>\n"
		    "<th>Latest commit</th>\n"
		    "<th>Name</th>");
		for (i = 0; i < rsp->n; i++)
			render_index_line(&rsp->repos[i]);
		puts("</table>");
	} else {
		puts("<p>No repositories in "SCAN_DIR"</p>");
	}
	render_footer();
}

static void
render_log(const struct repo *rp, size_t n, size_t p)
{
	puts("<table>\n"
	    "<tr>\n"
	    "<th>Date</th>\n"
	    "<th>Id</th>\n"
	    "<th>Subject</th>"
	    "</tr>");
	puts("</table>");
}

static void
render_tree(const struct repo *rp, const char *path)
{
	puts("<table>\n"
	    "<tr>\n"
	    "<th>Name</th>\n"
	    "<th>Size</th>\n"
	    "</tr>");
	puts("</table>");
}

static void
render_refs(const struct repo *rp)
{
	puts("<table>\n"
	    "<tr>\n"
	    "<th>Branch</th>\n"
	    "</tr>");
	puts("</table>");
	puts("<table>\n"
	    "<tr>\n"
	    "<th>Tag</th>\n"
	    "</tr>");
	puts("</table>");
}

static void
render_summary(const struct repo *rp)
{
	char h[REPO_NAME_MAX + 22 + 1];

	snprintf(h, sizeof(h), "<a href=/>Index</a> / %s", rp->name);

	http_headers("200 Success");
	render_header(rp->name, h);

	printf("<h2>\n"
	    "<a href=/%s/l>Log</a>\n"
	    "</h2>\n",
	    rp->name);
	render_log(rp, 3, 1);

	printf("<h2>\n"
	    "<a href=/%s/t>Tree</a>\n"
	    "</h2>\n",
	    rp->name);
	render_tree(rp, "/");

	puts("<h2>Refs</h2>");
	render_refs(rp);

	render_footer();
}

static void
route_repo(const char *url, struct repo *rp, size_t n)
{
	parse_repo(rp);

	if (url[n + 1] == '\0' || url[n + 2] == '\0')
		render_summary(rp);
	else
		render_notfound();

	git_repository_free(rp->handle);
}

int
main(int argc, char *argv[])
{
	char *url;
	struct repos rsp;
	size_t i, n;

	(void) argc;
	(void) argv;

	rsp.n = 0;
	rsp.repos = NULL;

	git_libgit2_init();

	url = getenv("PATH_INFO");

	if (!url || url[0] == '\0' || url[0] != '/') {
		render_notfound();
		goto cleanup;
	}

	find_repos(&rsp, SCAN_DIR, 0);

	if (url[1] == '\0') {
		render_index(&rsp);
		goto cleanup;
	}

	for (i = 0; i < rsp.n; i++) {
		n = strlen(rsp.repos[i].name);
		if (!strncmp(rsp.repos[i].name, url + 1, n) &&
		    (url[n + 1] == '\0' || url[n + 1] == '/')) {
			route_repo(url, &rsp.repos[i], n);
			goto cleanup;
		}
	}
	render_notfound();

cleanup:
	if (rsp.repos != NULL)
		free(rsp.repos);
	git_libgit2_shutdown();

	return 0;
}
