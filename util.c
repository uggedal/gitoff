#include "util.h"

const void
veprintf(const char *fmt, va_list ap)
{
	vfprintf(stderr, fmt, ap);
	if (fmt[0] && fmt[strlen(fmt) - 1] == ':') {
		fputc(' ', stderr);
		perror(NULL);
	}
}

void
eprintf(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	veprintf(fmt, ap);
	va_end(ap);

	exit(1);
}

void
weprintf(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	veprintf(fmt, ap);
	va_end(ap);
}

void
gvprintf(const char *fmt, va_list ap)
{
	const git_error *err;

	vfprintf(stderr, fmt, ap);

	if ((err = giterr_last()) != NULL && err->message != NULL) {
		fputc(' ', stderr);
		fputs(err->message, stderr);
	} else
		fputc('\n', stderr);
}

void
geprintf(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	gvprintf(fmt, ap);
	va_end(ap);

	exit(1);
}

void
gweprintf(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	gvprintf(fmt, ap);
	va_end(ap);
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
