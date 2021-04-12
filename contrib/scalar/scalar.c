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
	   "Commands: clone, config, diagnose, list\n"
	   "\tregister, run, unregister");

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

struct scalar_builtin {
	const char *name;
	int (*fn)(int, const char **);
};

struct scalar_builtin builtins[] = {
	{ "clone", cmd_clone },
	{ "config", cmd_config },
	{ "delete", cmd_delete },
	{ "diagnose", cmd_diagnose },
	{ "list", cmd_list },
	{ "register", cmd_register },
	{ "run", cmd_run },
	{ "unregister", cmd_unregister },
	{ NULL, NULL},
};

int cmd_main(int argc, const char **argv)
{
	int i = 0;

	if (argc < 2)
		usage(scalar_usage);

	while (builtins[i].name) {
		if (!strcmp(builtins[i].name, argv[1]))
			return builtins[i].fn(argc, argv);
		i++;
	}

	usage(scalar_usage);
	return -1;
}
