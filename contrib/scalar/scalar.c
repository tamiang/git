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
