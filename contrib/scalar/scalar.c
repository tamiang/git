/*
 * This is a port of Scalar to C.
 */

#include "cache.h"
#include "gettext.h"
#include "parse-options.h"

static const char scalar_usage[] =
	N_("scalar <command> [<options>]\n\n"
	   "Commands: clone, config, diagnose, list\n"
	   "\tregister, run, unregister");

static int cmd_clone(int argc, const char **argv)
{
	die(N_("'%s' not yet implemented"), argv[0]);
}

static int cmd_config(int argc, const char **argv)
{
	die(N_("'%s' not yet implemented"), argv[0]);
}

static int cmd_diagnose(int argc, const char **argv)
{
	die(N_("'%s' not yet implemented"), argv[0]);
}

static int cmd_list(int argc, const char **argv)
{
	die(N_("'%s' not yet implemented"), argv[0]);
}

static int cmd_register(int argc, const char **argv)
{
	die(N_("'%s' not yet implemented"), argv[0]);
}

static int cmd_run(int argc, const char **argv)
{
	die(N_("'%s' not yet implemented"), argv[0]);
}

static int cmd_unregister(int argc, const char **argv)
{
	die(N_("'%s' not yet implemented"), argv[0]);
}

struct {
	const char *name;
	int (*fn)(int, const char **);
	int needs_git_repo;
} builtins[] = {
	{ "clone", cmd_clone, 0 },
	{ "config", cmd_config, 1 },
	{ "diagnose", cmd_diagnose, 1 },
	{ "list", cmd_list, 0 },
	{ "register", cmd_register, 1 },
	{ "run", cmd_run, 1 },
	{ "unregister", cmd_unregister, 1 },
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
