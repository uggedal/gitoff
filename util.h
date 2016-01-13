#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <git2.h>

void eprintf(const char *, ...);
void geprintf(const char *, ...);

void htmlescchar(const char);
void htmlesc(const char *);
void urienc(const char *);

void abbrev(char *, size_t);
void printgt(const git_time_t);
void printgo(int);
