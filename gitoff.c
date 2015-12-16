#include <git2.h>

int
main(int argc, char *argv[])
{
	git_libgit2_init();
	git_libgit2_shutdown();

	return 0;
}
