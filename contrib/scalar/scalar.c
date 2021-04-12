/*
 * This is a port of Scalar to C.
 */

#include "git-compat-util.h"
#include "gettext.h"
#include "parse-options.h"
#include "strbuf.h"
#include "strvec.h"
#include "run-command.h"

static const char scalar_usage[] =
	N_("scalar <command> [<options>]\n\n"
	   "Commands: cache-server, clone, config, delete, diagnose, list\n"
	   "\tpause, register, resume, run, unregister, upgrade");

static int cmd_cache_server(int argc, const char **argv)
{
	die(N_("'%s' not yet implemented"), argv[0]);
}

static int cmd_clone(int argc, const char **argv)
{
	char *cache_server_url = NULL, *branch = NULL;
	int single_branch = 0, no_fetch_commits_and_trees = 0;
	char *local_cache_path = NULL;
	int full_clone = 0;
	struct option clone_options[] = {
		OPT_STRING(0, "cache-server-url", &cache_server_url,
			   N_("<url>"),
			   N_("The url or friendly name of the cache server")),
		OPT_STRING('b', "branch", &branch, N_("<branch>"),
			   N_("Branch to checkout after clone")),
		OPT_BOOL(0, "single-branch", &single_branch,
			 N_("Use this option to only download metadata for the branch that will be checked out")),
		OPT_BOOL(0, "no-fetch-commits-and-trees",
			 &no_fetch_commits_and_trees,
			 N_("Use this option to skip fetching commits and trees after clone")),
		OPT_STRING(0, "local-cache-path", &local_cache_path,
			   N_("<path>"),
			   N_("Use this option to override the path for the local Scalar cache.")),
		OPT_BOOL(0, "full-clone", &full_clone,
			 N_("When cloning, create full working directory.")),
		OPT_END(),
	};
	const char * const clone_usage[] = {
		N_("git clone [<options>] [--] <repo> [<dir>]"),
		NULL
	};

	argc = parse_options(argc, argv, NULL, clone_options, clone_usage, 0);

	if (argc != 1) {
		usage_msg_opt(N_("need a URL"), clone_usage, clone_options);
	}

	die(N_("'%s' not yet implemented"), argv[0]);
}

static int cmd_config(int argc, const char **argv)
{
	die(N_("'%s' not yet implemented"), argv[0]);
}

static int cmd_delete(int argc, const char **argv)
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

static int cmd_pause(int argc, const char **argv)
{
	die(N_("'%s' not yet implemented"), argv[0]);
}

static int cmd_register(int argc, const char **argv)
{
	die(N_("'%s' not yet implemented"), argv[0]);
}

static int cmd_resume(int argc, const char **argv)
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

static int cmd_upgrade(int argc, const char **argv)
{
	die(N_("'%s' not yet implemented"), argv[0]);
}

int cmd_main(int argc, const char **argv)
{
	if (argc < 2) {
		usage(scalar_usage);
	} else if (!strcmp(argv[1], "cache-server")) {
		return !!cmd_cache_server(argc - 1, argv + 1);
	} else if (!strcmp(argv[1], "clone")) {
		return !!cmd_clone(argc - 1, argv + 1);
	} else if (!strcmp(argv[1], "config")) {
		return !!cmd_config(argc - 1, argv + 1);
	} else if (!strcmp(argv[1], "delete")) {
		return !!cmd_delete(argc - 1, argv + 1);
	} else if (!strcmp(argv[1], "diagnose")) {
		return !!cmd_diagnose(argc - 1, argv + 1);
	} else if (!strcmp(argv[1], "list")) {
		return !!cmd_list(argc - 1, argv + 1);
	} else if (!strcmp(argv[1], "pause")) {
		return !!cmd_pause(argc - 1, argv + 1);
	} else if (!strcmp(argv[1], "register")) {
		return !!cmd_register(argc - 1, argv + 1);
	} else if (!strcmp(argv[1], "resume")) {
		return !!cmd_resume(argc - 1, argv + 1);
	} else if (!strcmp(argv[1], "run")) {
		return !!cmd_run(argc - 1, argv + 1);
	} else if (!strcmp(argv[1], "unregister")) {
		return !!cmd_unregister(argc - 1, argv + 1);
	} else if (!strcmp(argv[1], "upgrade")) {
		return !!cmd_upgrade(argc - 1, argv + 1);
	} else {
		usage(scalar_usage);
	}

	return 0;
}
