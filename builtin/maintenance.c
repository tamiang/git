#include "builtin.h"
#include "config.h"
#include "maintenance.h"
#include "parse-options.h"
#include "repository.h"

static char const * const builtin_maintenance_usage[] = {
	N_("git maintenance run [<options>]"),
	NULL
};
static char const * const subcommand_run_usage[] = {
	N_("git maintenance run"),
	NULL
};

static int subcommand_run(int argc, const char **argv, const char *prefix)
{
	static struct option subcommand_run_options[] = {
		OPT_END(),
	};

	argc = parse_options(argc, argv, prefix,
			     subcommand_run_options,
			     subcommand_run_usage,
			     0);

	post_command_maintenance(the_repository,
				 MAINTENANCE_OVERRIDE_CONFIG,
				 NULL);
	return 0;
}

int cmd_maintenance(int argc, const char **argv, const char *prefix)
{
	static struct option builtin_maintenance_options[] = {
		OPT_END(),
	};

	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage_with_options(builtin_maintenance_usage,
				   builtin_maintenance_options);

	git_config(git_default_config, NULL);
	argc = parse_options(argc, argv, prefix,
			     builtin_maintenance_options,
			     builtin_maintenance_usage,
			     PARSE_OPT_KEEP_UNKNOWN);

	if (argc > 0) {
		if (!strcmp(argv[0], "run"))
			return subcommand_run(argc, argv, prefix);
	}

	usage_with_options(builtin_maintenance_usage,
			   builtin_maintenance_options);
}
