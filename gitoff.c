#include <sys/stat.h>
#include <sys/types.h>

#include <ctype.h>
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
htmlescchar(const char c)
{
	switch(c) {
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
		putchar(c);
	}
}

void
htmlesc(const char *s)
{
	for (; s && *s; s++)
		htmlescchar(*s);
}

void
urienc(const char *s)
{
	for (; s && *s; s++)
		if ((unsigned char)*s <= 0x1F || (unsigned char)*s >= 0x7F ||
		    strchr(" <>\"%{}|\\^`", *s))
			printf("%%%02X", (unsigned char)*s);
		else
			putchar(*s);
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

	printf("%.4u-%02u-%02u&nbsp;%02u:%02u",
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
render_header(const char *title, const char *id)
{
	printf("<!doctype html>\n"
	    "<html>\n<head>\n"
	    "<title>%s</title>\n"
	    "<link href=/gitoff.css rel=stylesheet>\n"
	    "</head>\n<body id=%s>\n", title, id);
}

static void
render_title(const char *title)
{
	printf("<h1>%s</h1>\n", title);
}

static void
render_footer(void)
{
	puts("</body>\n</html>");
}

static void
render_notfound(void)
{
	http_headers("404 Not Found");
	render_header("404 Not Found", "404");
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
	render_header("Index", "index");
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
	    "<td>&nbsp;</td>\n"
	    "</tr>\n", rp->name, hex);
}

static void
render_log_line(const struct repo *rp, const git_commit *ci)
{
	char hex[GIT_OID_HEXSZ + 1];
	char title[TITLE_MAX + 1];
	const git_signature *sig;

	git_oid_tostr(hex, sizeof(hex), git_commit_id(ci));
	strlcpy(title, git_commit_message(ci), sizeof(title));
	abbrev(title, TITLE_MAX);


	puts("<tr>\n<td>");
	printgt(git_commit_time(ci));
	printf("</td>\n"
	    "<td><a href=/%s/c/%s>%.*s</a></td>\n"
	    "<td>", rp->name, hex, OBJ_ABBREV, hex);
	htmlesc(title);
	puts("</td>\n<td>");
	if ((sig = git_commit_author(ci)) != NULL)
		htmlesc(sig->name);
	else
		puts("&nbsp;");
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

	puts("<div class=log>\n<table>\n"
	    "<tr>\n"
	    "<th>Date</th>\n"
	    "<th>Id</th>\n"
	    "<th>Subject</th>"
	    "<th>Author</th>"
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

	puts("</table>\n</div>");
}

static void
render_log(const struct repo *rp, const char *rev)
{
	http_headers("200 Success");
	render_header(rp->name, "log");
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

	puts("<div class=tree>\n<table>\n"
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

	if (base[0] != '\0') {
		printf("<tr>\n<td colspan=2><a href=/%s/t", rp->name);
		if (strlen(parent))
			putchar('/');
		urienc(parent);
		puts(">..</a>/</td>\n</tr>");
	}

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

		printf("<tr>\n<td><a href=/%s/t/", rp->name);
		urienc(base);
		if (strlen(base))
		        putchar('/');
		urienc(git_tree_entry_name(te));
		putchar('>');
		htmlesc(git_tree_entry_name(te));
		fputs("</a>", stdout);
		if (dec != '\0')
			putchar(dec);
		fputs("</td>\n<td class=r>", stdout);
		if (size > 0)
			printf("%zu", size);
		else
			putchar('-');
		puts("</td>\n</tr>");
		git_object_free(obj);
	}

	puts("</table>\n</div>");
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
	render_header(rp->name, "tree");
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
	git_object *obj;
	char hex[GIT_OID_HEXSZ + 1];

	for (i = 0; i < 2; i++) {
		if (strncmp(git_reference_name(ref), prefixes[i],
		    strlen(prefixes[i])))
			continue;

		if (git_reference_type(ref) == GIT_REF_SYMBOLIC)
			if (git_reference_resolve(&res, ref))
				geprintf("ref resolve");

		if (git_reference_peel(&obj, res ? res : ref, GIT_OBJ_ANY))
			geprintf("ref peel");
		git_oid_tostr(hex, sizeof(hex), git_object_id(obj));

		fputs("<tr>\n<td>", stdout);
		htmlesc(git_reference_shorthand(ref));
		printf("</td>\n<td><a href=/%s/c/%s>%.*s</a></td>\n"
		    "<td>%s</td>\n</tr>\n",
		    rp->name, hex, OBJ_ABBREV, hex,
		    i == 0 ? "Branch" : "Tag");

		git_object_free(obj);
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
	render_header(rp->name, "summary");
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
render_signature(const char *t1, const char *t2, const git_signature *sig)
{
	printf("<tr>\n<td class=b>%s</td>\n<td>", t1);
	htmlesc(sig->name);
	htmlesc(" <");
	htmlesc(sig->email);
	htmlesc(">");
	puts("</td>\n</tr>");
	printf("<tr>\n<td class=b>%s</td>\n<td>", t2);
	printgt(sig->when.time);
	puts(" ");
	printgo(sig->when.offset);
	puts("</td>\n</tr>");
}

static void
render_commit_header(const struct repo *rp, git_commit *ci)
{
	const git_signature *s1, *s2;
	unsigned int i, n;
	char hex[GIT_OID_HEXSZ + 1];

	if ((s1 = git_commit_author(ci)) != NULL)
		render_signature("Author", "Date", s1);
	if ((s2 = git_commit_committer(ci)) != NULL &&
	    strcmp(s1->name, s2->name) && strcmp(s1->email, s2->email))
		render_signature("Committer", "Commit date", s2);

	if ((n = git_commit_parentcount(ci)) > 0) {
		printf("<tr>\n"
		    "<td class=b>Parent%s</td>\n"
		    "<td>", n > 1 ? "s" : "");
		for (i = 0; i < n; i++) {
			git_oid_tostr(hex, sizeof(hex),
			    git_commit_parent_id(ci, i));
			printf("<a href=/%s/c/%s>%.*s</a> ",
			    rp->name, hex, OBJ_ABBREV, hex);
		}
		puts("</td>\n</tr>");
	}
}

static void
render_commit_stats(git_diff *diff)
{
	const git_diff_delta *delta;
	git_patch *patch;
	size_t n, i, add, del, total_add, total_del;

	total_add = 0;
	total_del = 0;

	for (i = 0, n = git_diff_num_deltas(diff); i < n; i++) {
		patch = NULL;
		if (git_patch_from_diff(&patch, diff, i))
			geprintf("patch from diff");

		delta = git_patch_get_delta(patch);

		printf("<tr>\n<td><a href=#f%zu>", i);
		htmlesc(delta->old_file.path);
		fputs("</a>", stdout);
		if (strcmp(delta->old_file.path, delta->new_file.path)) {
			fputs(" => ", stdout);
			htmlesc(delta->new_file.path);
		}

		puts("</td>\n");

		if (delta->flags & GIT_DIFF_FLAG_BINARY)
			printf("<td colspan=2>%" PRIu64 " -> %" PRIu64
			    " bytes</td>",
			    delta->old_file.size, delta->new_file.size);
		else {
			add = 0;
			del = 0;
			if (git_patch_line_stats(NULL, &add, &del, patch))
				geprintf("patch line stats");

			total_add += add;
			total_del += del;

			printf("<td class='a r'>+%zu</td>"
			    "<td class='d r'>-%zu</td>\n", add, del);

			git_patch_free(patch);
		}
		puts("</tr>");
	}
	if (n > 1)
		printf("<tr>\n<td>%zu files</td>\n<td class='a r'>+%zu</td>\n"
		    "<td class='d r'>-%zu</td>\n</tr>\n",
		    n, total_add, total_del);
}

static int
render_diff_line(const git_diff_delta *delta, const git_diff_hunk *hunk,
    const git_diff_line *line, void *data)
{
	size_t i, *nfiles;
	char c;

	nfiles = data;
	c = '\0';

	(void)delta;
	(void)hunk;

	switch (line->origin) {
	case GIT_DIFF_LINE_ADDITION:
	case GIT_DIFF_LINE_ADD_EOFNL:
		c = 'a';
		break;
	case GIT_DIFF_LINE_DELETION:
	case GIT_DIFF_LINE_DEL_EOFNL:
		c = 'd';
		break;
	case GIT_DIFF_LINE_FILE_HDR:
		c = 'f';
		break;
	case GIT_DIFF_LINE_HUNK_HDR:
		c = 'h';
		break;
	default:
		break;
	}

	if (c != '\0') {
		printf("<span class=%c", c);
		if (c == 'f') {
			printf(" id=f%zu", *nfiles);
			*nfiles = *nfiles + 1;
		}
		putchar('>');
	}

	if (line->origin == GIT_DIFF_LINE_CONTEXT ||
	    line->origin == GIT_DIFF_LINE_ADDITION ||
	    line->origin == GIT_DIFF_LINE_DELETION)
		putchar(line->origin);

	for (i = 0; i < line->content_len - (c == '\0' ? 0 : 1); i++)
		htmlescchar(line->content[i]);

	if (c != '\0')
		puts("</span>");

	return 0;
}

static void
render_commit(const struct repo *rp, const char *rev)
{
	int e;
	git_object *obj = NULL;
	git_commit *ci = NULL;
	git_commit *parent = NULL;
	git_tree *tree, *parent_tree;
	git_diff *diff;
	git_diff_options opts;
	git_diff_find_options find_opts;
	const git_oid *id;
	char hex[GIT_OID_HEXSZ + 1];
	size_t nfiles;

	nfiles = 0;

	if (git_revparse_single(&obj, rp->handle, rev)) {
		render_notfound();
		return;
	}

	id = git_object_id(obj);
	git_oid_tostr(hex, sizeof(hex), id);

	e = git_commit_lookup(&ci, rp->handle, id);
	if (e == GIT_ENOTFOUND) {
		render_notfound();
		return;
	} else if (e)
		geprintf("commit lookup");

	http_headers("200 Success");
	render_header(rp->name, "commit");
	printf("<h1><a href=/>Index</a> / <a href=/%s>%s</a> / ",
	    rp->name, rp->name);
	htmlesc(hex);
	puts("</h1>");

	puts("<table>");
	render_commit_header(rp, ci);
	puts("</table>");

	puts("<pre id=msg>");
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
	if (git_diff_tree_to_tree(&diff, rp->handle, parent_tree, tree, &opts))
		geprintf("diff tree to tree");
	git_diff_find_init_options(&find_opts, GIT_DIFF_FIND_OPTIONS_VERSION);
	if (git_diff_find_similar(diff, &find_opts))
		geprintf("diff find similar");

	puts("<div id=stats>\n<table>");
	render_commit_stats(diff);
	puts("</table>\n</div>");

	puts("<pre id=diff>");
	git_diff_print(diff, GIT_DIFF_FORMAT_PATCH, render_diff_line, &nfiles);
	puts("</pre>");

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
