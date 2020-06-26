#include "builtin.h"
#include "repository.h"
#include "config.h"
#include "lockfile.h"
#include "parse-options.h"
#include "run-command.h"
#include "argv-array.h"
#include "hashmap.h"

#define MAX_NUM_TASKS 1

static const char * const builtin_maintenance_usage[] = {
	N_("git maintenance run [<options>]"),
	NULL
};

struct maintenance_opts {
	int auto_flag;
	int quiet;
} opts;

typedef int maintenance_task_fn(struct repository *r);

struct maintenance_task {
	struct hashmap_entry ent;
	const char *name;
	maintenance_task_fn *fn;
};

static int task_entry_cmp(const void *unused_cmp_data,
			  const struct hashmap_entry *eptr,
			  const struct hashmap_entry *entry_or_key,
			  const void *keydata)
{
	const struct maintenance_task *e1, *e2;
	const char *name = keydata;

	e1 = container_of(eptr, const struct maintenance_task, ent);
	e2 = container_of(entry_or_key, const struct maintenance_task, ent);

	return strcasecmp(e1->name, name ? name : e2->name);
}

struct maintenance_task tasks[MAX_NUM_TASKS];
int num_tasks;
struct hashmap task_map;

static int maintenance_task_gc(struct repository *r)
{
	int result;
	struct argv_array cmd = ARGV_ARRAY_INIT;

	argv_array_pushl(&cmd, "gc", NULL);

	if (opts.auto_flag)
		argv_array_pushl(&cmd, "--auto", NULL);
	if (opts.quiet)
		argv_array_pushl(&cmd, "--quiet", NULL);

	result = run_command_v_opt(cmd.argv, RUN_GIT_CMD);
	argv_array_clear(&cmd);

	return result;
}

static int maintenance_run(struct repository *r)
{
	int i;
	int result = 0;

	for (i = 0; !result && i < num_tasks; i++)
		result = tasks[i].fn(r);

	return result;
}

static void initialize_tasks(void)
{
	int i;
	num_tasks = 0;

	tasks[num_tasks].name = "gc";
	tasks[num_tasks].fn = maintenance_task_gc;
	num_tasks++;

	hashmap_init(&task_map, task_entry_cmp, NULL, MAX_NUM_TASKS);

	for (i = 0; i < num_tasks; i++) {
		hashmap_entry_init(&tasks[i].ent,
				   strihash(tasks[i].name));
		hashmap_add(&task_map, &tasks[i].ent);
	}
}

int cmd_maintenance(int argc, const char **argv, const char *prefix)
{
	struct repository *r = the_repository;

	static struct option builtin_maintenance_options[] = {
		OPT_BOOL(0, "auto", &opts.auto_flag,
			 N_("run tasks based on the state of the repository")),
		OPT_BOOL(0, "quiet", &opts.quiet,
			 N_("do not report progress or other information over stderr")),
		OPT_END()
	};

	memset(&opts, 0, sizeof(opts));

	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage_with_options(builtin_maintenance_usage,
				   builtin_maintenance_options);

	initialize_tasks();

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
