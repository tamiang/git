#include "git-compat-util.h"
#include "config.h"
#include "packfile.h"
#include "repository.h"
#include "run-command.h"
#include "maintenance.h"

void post_command_maintenance(struct repository *r, int flags,
			      post_process_func ppf)
{
	struct argv_array argv_gc_auto = ARGV_ARRAY_INIT;
	struct child_process proc = CHILD_PROCESS_INIT;
	int post_command_enabled = 1;

	repo_config_get_bool(r, "jobs.post-command.enabled", &post_command_enabled);

	if (!post_command_enabled || !(flags & MAINTENANCE_OVERRIDE_CONFIG))
		return;

	argv_array_pushl(&argv_gc_auto, "gc", "--auto", NULL);
	if (flags & MAINTENANCE_QUIET)
		argv_array_push(&argv_gc_auto, "--quiet");

	proc.no_stdin = 1;
	proc.stdout_to_stderr = 1;
	proc.err = (flags & MAINTENANCE_REDIRECT_ERROR) ? -1 : 0;
	proc.git_cmd = 1;
	proc.argv = argv_gc_auto.argv;

	close_object_store(r->objects);

	if (!start_command(&proc)) {
		if (ppf)
			ppf(&proc);
		finish_command(&proc);
	}
}