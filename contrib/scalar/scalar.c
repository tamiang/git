/*
 * This is a port of Scalar to C.
 */

#include "cache.h"
#include "gettext.h"
#include "parse-options.h"

static const char scalar_usage[] =
	N_("scalar <command> [<options>]");

struct {
	const char *name;
	int (*fn)(int, const char **);
	int needs_git_repo;
} builtins[] = {
	{ NULL, NULL},
};

int cmd_main(int argc, const char **argv)
{
	int i;

	if (argc < 2)
		usage(scalar_usage);
	argv++;
	argc--;

	for (i = 0; builtins[i].name; i++)
		if (!strcmp(builtins[i].name, argv[0])) {
			if (builtins[i].needs_git_repo)
				setup_git_directory();
			return builtins[i].fn(argc, argv);
		}

	usage(scalar_usage);
}
