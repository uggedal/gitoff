#include <sys/stat.h>
#include <sys/types.h>

#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <libgen.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <git2.h>

#define REPO_NAME_MAX 64
#define OBJ_ABBREV 7
#define TITLE_MAX 50

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
	exit(1);
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
		fputs(err->message, stderr);
	} else
		fputc('\n', stderr);
	exit(1);
}

void
htmlesc(const char *s)
{
	for (; s && *s; s++) {
		switch(*s) {
		case '&':
			fputs("&amp;", stdout);
			break;
		case '<':
			fputs("&lt;", stdout);
			break;
		case '>':
			fputs("&gt;", stdout);
			break;
		case '\"':
			fputs("&#34;", stdout);
			break;
		case '\'':
			fputs("&#39;", stdout);
			break;
		default:
			putchar(*s);
		}
	}
}

void
abbrev(char *s, size_t n)
{
	if (strlen(s) >= n) {
		s[n + 1] = '\0';
		s[n] = '.';
		s[n - 1] = '.';
		s[n - 2] = '.';
	}
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

void
printgo(int o)
{
	int h, m;
	char sign;

	sign = '+';

	if (o < 0) {
		sign = '-';
		o = -o;
	}

	h = o / 60;
	m = o % 60;

	printf("%c%02d%02d", sign, h, m);
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
		geprintf("repo head %s:", rp->path);
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

static int
repocmp(const void *va, const void *vb)
{
	const struct repo *a = va, *b = vb;
	return b->age - a->age;
}

static void
http_headers(const char *status)
{
	printf("Content-Type: text/html; charset=UTF-8\n"
	    "Status: %s\n\n", status);
}

static void
render_header(const char *title)
{
	printf("<!doctype html>\n"
	    "<html>\n"
	    "<head>\n"
	    "<title>%s</title>\n"
	    "<link href=/gitoff.css rel=stylesheet>\n"
	    "</head>\n"
	    "<body>\n", title);
}

static void
render_title(const char *title)
{
	printf("<h1>%s</h1>\n", title);
}

static void
render_footer()
{
	puts("</body>\n</html>");
}

static void
render_notfound(void)
{
	http_headers("404 Not Found");
	render_header("404 Not Found");
	render_title("404 Not Found");
	render_footer();
}

static void
render_index_line(const struct repo *rp)
{
	puts("<tr>\n<td>");
	printgt(rp->age);
	printf("</td>\n"
	    "<td><a href=/%s>%s</a></td>\n"
	    "</tr>\n", rp->name, rp->name);
}

static void
render_index(const struct repos *rsp)
{
	size_t i;

	parse_repos(rsp);
	qsort(rsp->repos, rsp->n, sizeof(*rsp->repos), repocmp);

	http_headers("200 Success");
	render_header("Index");
	render_title("Index");
	if (rsp->n > 0) {
		puts("<table>\n"
		    "<tr>\n"
		    "<th>Latest commit</th>\n"
		    "<th>Name</th>"
		    "</tr>");
		for (i = 0; i < rsp->n; i++)
			render_index_line(&rsp->repos[i]);
		puts("</table>");
	} else
		puts("<p>No repositories</p>");
	render_footer();
}

static void
render_log_link(const struct repo *rp, const git_commit *ci)
{
	char hex[GIT_OID_HEXSZ + 1];

	git_oid_tostr(hex, sizeof(hex), git_commit_id(ci));

	printf("<tr>\n"
	    "<td>&nbsp;</td>\n"
	    "<td><a href=/%s/l/%s>Next &raquo;</a></td>\n"
	    "<td>&nbsp;</td>\n"
	    "</tr>\n", rp->name, hex);
}

static void
render_log_line(const struct repo *rp, const git_commit *ci)
{
	char hex[GIT_OID_HEXSZ + 1];
	char title[TITLE_MAX + 1];

	git_oid_tostr(hex, sizeof(hex), git_commit_id(ci));
	strlcpy(title, git_commit_message(ci), sizeof(title));
	abbrev(title, TITLE_MAX);

	puts("<tr>\n<td>");
	printgt(git_commit_time(ci));
	printf("</td>\n"
	    "<td><a href=/%s/c/%s>%.*s</a></td>\n"
	    "<td>", rp->name, hex, OBJ_ABBREV, hex);
	htmlesc(title);
	puts("</td>\n</tr>");
}

static void
render_log_list(const struct repo *rp, size_t n, const char *rev)
{
	git_revwalk *w;
	git_object *obj = NULL;
	git_commit *ci = NULL;
	git_oid id;
	size_t i;

	puts("<table>\n"
	    "<tr>\n"
	    "<th>Date</th>\n"
	    "<th>Id</th>\n"
	    "<th>Subject</th>"
	    "</tr>");

	if (git_revwalk_new(&w, rp->handle))
		geprintf("revwalk new %s:", rp->path);
	if (rev && strlen(rev) > 0) {
		if (git_revparse_single(&obj, rp->handle, rev))
			geprintf("revparse single %s:", rp->path);
		if (git_revwalk_push(w, git_object_id(obj)))
			geprintf("revwalk push %s:", rp->path);
	} else {
		if (git_revwalk_push_head(w))
			geprintf("revwalk push head %s:", rp->path);
	}

	git_revwalk_sorting(w, GIT_SORT_TIME);

	for (i = 0; !git_revwalk_next(&id, w); i++, git_commit_free(ci)) {
		if (git_commit_lookup(&ci, rp->handle, &id))
			geprintf("commit lookup %s:", rp->path);
		if (n > 0 && i >= n)
			break;
		else if (i == 1000) {
			render_log_link(rp, ci);
			break;
		}
		render_log_line(rp, ci);
	}

	git_revwalk_free(w);
	if (obj)
		git_object_free(obj);

	puts("</table>");
}

static void
render_log(const struct repo *rp, const char *rev)
{
	http_headers("200 Success");
	render_header(rp->name);
	printf("<h1><a href=/>Index</a> / <a href=/%s>%s</a> / log</h1>\n",
	    rp->name, rp->name);
	render_log_list(rp, 0, rev);
	render_footer();
}

static void
render_tree_list(const struct repo *rp, const git_tree *t, const char *base)
{
	char *parent;
	const git_tree_entry *te;
	git_object *obj;
	size_t i, n, size;
	char dec;

	puts("<table>\n"
	    "<tr>\n"
	    "<th>Name</th>\n"
	    "<th>Size</th>\n"
	    "</tr>");

	if (!(parent = strdup(base)))
		eprintf("strdup:");
	if (!(parent = dirname(parent)))
		eprintf("dirname:");

	if (parent[0] == '.' && parent[1] == '\0')
		parent[0] = '\0';

	if (base[0] != '\0')
		printf("<tr>\n"
		    "<td><a href=/%s/t%s%s>..</a>/</td>\n"
		    "<td>&nbsp;</td>\n"
		    "</tr>\n", rp->name, strlen(parent) ? "/" : "\0", parent);

	for (i = 0, n = git_tree_entrycount(t); i < n; i++) {
		if ((te = git_tree_entry_byindex(t, i)) == NULL)
			geprintf("tree entry byindex %s:", rp->path);
		if (git_tree_entry_to_object(&obj, rp->handle, te))
			geprintf("tree entry to object %s:", rp->path);

		dec = '\0';
		size = 0;

		switch (git_object_type(obj)) {
		case GIT_OBJ_TREE:
			dec = '/';
			break;
		case GIT_OBJ_BLOB:
			size = git_odb_object_size((git_odb_object *)obj);
			break;
		default:
			git_object_free(obj);
			continue;
		}

		printf("<tr>\n"
		    "<td><a href=/%s/t/%s%s%s>%s</a>%c</td>\n"
		    "<td class=r>\n", rp->name, base,
		    strlen(base) ? "/" : "\0", git_tree_entry_name(te),
		    git_tree_entry_name(te), dec);
		if (size > 0)
			printf("%zu", size);
		else
			putchar('-');
		puts("</td>\n</tr>");
		git_object_free(obj);
	}

	puts("</table>");
}

static void
render_tree_blob(const git_blob *b)
{
	char *nfmt = "<a href=#l%d id=l%d>%d</a>\n";
	const char *s;
	git_off_t len;
	off_t i = 0;
	size_t n = 1;

	if (git_blob_is_binary(b)) {
		puts("<p>Binary file</p>");
		return;
	}

	s = git_blob_rawcontent(b);
	len = git_blob_rawsize(b);

	puts("<table id=blob>\n"
	    "<tr>\n"
	    "<td class=r>\n"
	    "<pre>");

	if (n) {
		printf(nfmt, n, n, n);
		while (i < len - 1) {
			if (s[i] == '\n') {
				n++;
				printf(nfmt, n, n, n);
			}
			i++;
		}
	}

	puts("</pre>\n"
	    "</td>\n"
	    "<td>\n"
	    "<pre>");
	htmlesc(s);
	puts("</pre>\n"
	    "</td>\n"
	    "</tr>\n"
	    "</table>");
}

static void
render_tree_lookup(const struct repo *rp, const char *path)
{
	git_reference *ref;
	git_commit *ci;
	git_tree *t;
	git_tree_entry *te = NULL;
	git_object *obj;

	if (git_repository_head(&ref, rp->handle))
		geprintf("repo head %s:", rp->path);
	if (git_commit_lookup(&ci, rp->handle, git_reference_target(ref)))
		geprintf("commit lookup %s:", rp->path);
	if (git_commit_tree(&t, ci))
		geprintf("commit tree %s:", rp->path);

	git_commit_free(ci);

	if (path[0] == '\0') {
		render_tree_list(rp, (git_tree *)t, path);
		return;
	}

	if (git_tree_entry_bypath(&te, t, path)) {
		puts("<p>Not found</p>");
		return;
	}

	if (git_tree_entry_to_object(&obj, rp->handle, te))
		geprintf("tree entry to object %s:", rp->path);

	switch (git_object_type(obj)) {
	case GIT_OBJ_TREE:
		render_tree_list(rp, (git_tree *)obj, path);
		git_object_free(obj);
		break;
	case GIT_OBJ_BLOB:
		render_tree_blob((git_blob *)obj);
		git_object_free(obj);
		break;
	default:
		break;
	}

	git_object_free(obj);
	git_tree_entry_free(te);
}

static void
render_tree(const struct repo *rp, const char *path)
{
	http_headers("200 Success");
	render_header(rp->name);
	printf("<h1><a href=/>Index</a> / <a href=/%s>%s</a> / ",
	    rp->name, rp->name);
	htmlesc(path);
	puts("</h1>");
	render_tree_lookup(rp, path);
	render_footer();
}

static int
render_ref_item(git_reference *ref, void *data)
{
	const char *prefixes[] = {"refs/heads/", "refs/tags/"};
	int i;
	struct repo *rp = data;
	git_reference *res = NULL;
	const git_oid *oid;
	char hex[GIT_OID_HEXSZ + 1];

	for (i = 0; i < 2; i++) {
		if (strncmp(git_reference_name(ref), prefixes[i],
		    strlen(prefixes[i])))
			continue;

		if (git_reference_type(ref) == GIT_REF_SYMBOLIC)
			if (git_reference_resolve(&res, ref))
				geprintf("ref resolve");

		oid = git_reference_target(res ? res : ref);
		git_oid_tostr(hex, sizeof(hex), oid);

		printf("<tr>\n"
		    "<td>%s</td>\n"
		    "<td><a href=/%s/c/%s>%.*s</a></td>\n"
		    "<td>%s</td>\n"
		    "</tr>\n",
		    git_reference_name(ref) + strlen(prefixes[i]), rp->name,
		    hex, OBJ_ABBREV, hex, i == 0 ? "Branch" : "Tag");

		if (res)
			git_reference_free(res);
	}
	return 0;
}

static void
render_refs(const struct repo *rp)
{
	puts("<table>\n"
	    "<tr>\n"
	    "<th>Name</th>\n"
	    "<th>Id</th>\n"
	    "<th>Type</th>\n"
	    "</tr>");
	git_reference_foreach(rp->handle, &render_ref_item, (struct repo *)rp);
	puts("</table>");
}

static void
render_summary(const struct repo *rp)
{
	http_headers("200 Success");
	render_header(rp->name);
	printf("<h1><a href=/>Index</a> / %s</h1>\n", rp->name);

	printf("<h2><a href=/%s/l>Log</a></h2>\n", rp->name);
	render_log_list(rp, 3, NULL);

	printf("<h2><a href=/%s/t>Tree</a></h2>\n", rp->name);
	render_tree_lookup(rp, "\0");

	puts("<h2>Refs</h2>");
	render_refs(rp);

	render_footer();
}

static void
render_signature(const char *title, const git_signature *sig)
{
	printf("<tr>\n<td>%s</td>\n<td>", title);
	htmlesc(sig->name);
	htmlesc(" <");
	htmlesc(sig->email);
	htmlesc(">");
	puts("</td>\n</tr>");
	puts("<tr>\n<td>Date</td><td>");
	printgt(sig->when.time);
	puts(" ");
	printgo(sig->when.offset);
	puts("</td>\n</tr>");
}

static void
render_commit(const struct repo *rp, const char *rev)
{
	git_object *obj = NULL;
	git_commit *ci = NULL;
	git_commit *parent = NULL;
	git_tree *tree, *parent_tree;
	git_diff *diff;
	git_diff_options opts;
	const git_oid *id;
	const git_signature *sig;
	char hex[GIT_OID_HEXSZ + 1];
	unsigned int i, n;

	if (git_revparse_single(&obj, rp->handle, rev)) {
		render_notfound();
		return;
	}

	id = git_object_id(obj);
	git_oid_tostr(hex, sizeof(hex), id);

	if (git_commit_lookup(&ci, rp->handle, id))
		geprintf("commit lookup");

	http_headers("200 Success");
	render_header(rp->name);
	printf("<h1><a href=/>Index</a> / %s / ", rp->name);
	htmlesc(hex);
	puts("</h1>");

	puts("<table>");

	if ((sig = git_commit_committer(ci)) != NULL)
		render_signature("Committer", sig);
	if ((sig = git_commit_author(ci)) != NULL)
		render_signature("Author", sig);

	if ((n = git_commit_parentcount(ci)) > 0) {
		printf("<tr>\n"
		    "<td>Parent%c</td>\n"
		    "<td>", n > 1 ? 's' : '\0');
		for (i = 0; i < n; i++) {
			git_oid_tostr(hex, sizeof(hex),
			    git_commit_parent_id(ci, i));
			printf("<a href=/%s/c/%s>%.*s</a> ",
			    rp->name, hex, OBJ_ABBREV, hex);
		}
		puts("</td>\n</tr>");
	}

	puts("</table>");

	puts("<pre>");
	htmlesc(git_commit_message(ci));
	puts("</pre>");

	if (git_commit_tree(&tree, ci))
		geprintf("commit tree");

	if (!git_commit_parent(&parent, ci, 0)) {
		if (git_commit_tree(&parent_tree, parent))
			geprintf("commit tree");
	} else {
		parent = NULL;
		parent_tree = NULL;
	}

	git_diff_init_options(&opts, GIT_DIFF_OPTIONS_VERSION);
	if (git_diff_tree_to_tree(&diff, rp->handle, tree, parent_tree, &opts))
		geprintf("diff tree to tree");

	git_diff_free(diff);
	git_tree_free(tree);
	git_tree_free(parent_tree);
	git_commit_free(ci);

	render_footer();
}

static int
urlsep(const char *s)
{
	return s[0] == '\0' || s[0] == '/';
}

static void
route_repo(const char *url, struct repo *rp)
{
	char *p;
	size_t n;

	p = strdup(url);
	n = strlen(p);
	if (p[n-1] == '/')
		p[n-1] = '\0';
	parse_repo(rp);

	if (p[0] == '\0' || p[1] == '\0')
		render_summary(rp);
	else if (p[1] == 'l' && urlsep(p + 2))
		render_log(rp, p[2] == '\0' ? "\0" : p + 3);
	else if (p[1] == 't' && urlsep(p + 2))
		render_tree(rp, p[2] == '\0' ? "\0" : p + 3);
	else if (p[1] == 'c' && p[2] == '/')
		render_commit(rp, p + 3);
	else
		render_notfound();

	free(p);
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
		    urlsep(url + n + 1)) {
			route_repo(url + n + 1, &rsp.repos[i]);
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
