/*
 * This is a port of Scalar to C.
 */

#include "cache.h"
#include "gettext.h"
#include "parse-options.h"
#include "strbuf.h"
#include "strvec.h"
#include "run-command.h"
#include "config.h"
#include "strvec.h"

static const char scalar_usage[] =
	N_("scalar <command> [<options>]\n\n"
	   "Commands: clone, config, diagnose, list\n"
	   "\tregister, run, unregister");

struct config_setting {
	const char *key;
	const char *value;
};

static int set_recommended_config(void)
{
	int i = 0;
	struct config_setting config[] = {
		{ "am.keepcr", "true" },
		{ "commitGraph.generationVersion", "1" },
		{ "core.autocrlf", "false" },
		{ "core.fscache", "true" },
		{ "core.logallrefupdates", "true" },
		{ "core.multiPackIndex", "true" },
		{ "core.preloadIndex", "true" },
		{ "core.safecrlf", "false" },
		{ "credential.validate", "false" },
		{ "feature.manyFiles", "false" },
		{ "feature.experimental", "false" },
		{ "fetch.unpackLimit", "1" },
		{ "fetch.writeCommitGraph", "false" },
		{ "gc.auto", "0" },
		{ "gui.gcwarning", "false" },
		{ "index.threads", "true" },
		{ "index.version", "4" },
		{ "maintenance.auto", "false" },
		{ "merge.stat", "false" },
		{ "merge.renames", "false" },
		{ "pack.useBitmaps", "false" },
		{ "pack.useSparse", "true" },
		{ "receive.autogc", "false" },
		{ "reset.quiet", "true" },
		{ "status.aheadbehind", "false" },

#ifdef MINGW
		/*
		 * Windows-specific settings.
		 */
		{ "core.untrackedCache", "true" },
		{ "core.filemode", "true" },
#endif
		{ NULL, NULL },
	};

	while (config[i].key) {
		char *value;

		if (git_config_get_string(config[i].key, &value)) {
			trace2_data_string("scalar", the_repository, config[i].key, "created");
			git_config_set_gently(config[i].key, config[i].value);
		} else {
			trace2_data_string("scalar", the_repository, config[i].key, "exists");
			free(value);
		}
		i++;
	}

	return 0;
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

static int cmd_diagnose(int argc, const char **argv)
{
	die(N_("'%s' not yet implemented"), argv[0]);
}

static int cmd_list(int argc, const char **argv)
{
	die(N_("'%s' not yet implemented"), argv[0]);
}

static int initialize_enlistment_id(void)
{
	const char *key = "scalar.enlistment-id";
	char *value;
	struct strbuf id = STRBUF_INIT;
	int res;

	if (!git_config_get_string(key, &value)) {
		trace2_data_string("scalar", the_repository, "enlistment-id", value);
		free(value);
		return 0;
	}

	strbuf_addstr(&id, "TODO:GENERATE-GUID");

	trace2_data_string("scalar", the_repository, "enlistment-id", id.buf);
	res = git_config_set_gently(key, id.buf);
	strbuf_release(&id);
	return res;
}

static int toggle_maintenance(int enable)
{
	struct strvec args = STRVEC_INIT;

	strvec_push(&args, "maintenance");

	if (enable)
		strvec_push(&args, "start");
	else
		strvec_push(&args, "unregister");

	return run_command_v_opt(args.v, RUN_GIT_CMD);
}

static int cmd_register(int argc, const char **argv)
{
	int res = 0;

	res = res || initialize_enlistment_id();
	res = res || set_recommended_config();
	res = res || toggle_maintenance(1);

	return res;
}

static int cmd_run(int argc, const char **argv)
{
	die(N_("'%s' not yet implemented"), argv[0]);
}

static int cmd_unregister(int argc, const char **argv)
{
	toggle_maintenance(0);
	return 0;
}

struct scalar_builtin {
	const char *name;
	int (*fn)(int, const char **);
	int needs_git_repo;
};

struct scalar_builtin builtins[] = {
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
	int i = 0;

	if (argc < 2)
		usage(scalar_usage);

	while (builtins[i].name) {
		if (!strcmp(builtins[i].name, argv[1])) {
			if (builtins[i].needs_git_repo)
				setup_git_directory();
			return builtins[i].fn(argc, argv);
		}
		i++;
	}

	usage(scalar_usage);
	return -1;
}
