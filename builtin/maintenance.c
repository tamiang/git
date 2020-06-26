#include "builtin.h"
#include "repository.h"
#include "config.h"
#include "lockfile.h"
#include "parse-options.h"
#include "run-command.h"
#include "argv-array.h"

static const char * const builtin_maintenance_usage[] = {
	N_("git maintenance run [<options>]"),
	NULL
};

struct maintenance_opts {
	int auto_flag;
} opts;

static int maintenance_task_gc(struct repository *r)
{
	int result;
	struct argv_array cmd = ARGV_ARRAY_INIT;

	argv_array_pushl(&cmd, "gc", NULL);

	if (opts.auto_flag)
		argv_array_pushl(&cmd, "--auto", NULL);

	result = run_command_v_opt(cmd.argv, RUN_GIT_CMD);
	argv_array_clear(&cmd);

	return result;
}

static int maintenance_run(struct repository *r)
{
	return maintenance_task_gc(r);
}

int cmd_maintenance(int argc, const char **argv, const char *prefix)
{
	struct repository *r = the_repository;

	static struct option builtin_maintenance_options[] = {
		OPT_BOOL(0, "auto", &opts.auto_flag,
			 N_("run tasks based on the state of the repository")),
		OPT_END()
	};

	memset(&opts, 0, sizeof(opts));

	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage_with_options(builtin_maintenance_usage,
				   builtin_maintenance_options);

	argc = parse_options(argc, argv, prefix,
			     builtin_maintenance_options,
			     builtin_maintenance_usage,
			     PARSE_OPT_KEEP_UNKNOWN);

	if (argc == 1) {
		if (!strcmp(argv[0], "run"))
			return maintenance_run(r);
	}

	usage_with_options(builtin_maintenance_usage,
			   builtin_maintenance_options);
}
