#include "git-compat-util.h"
#include "gettext.h"
#include "jobs.h"
#include "config.h"
#include "run-command.h"
#include "strbuf.h"
#include "string-list.h"

static int fetch_remote(const char *repo, const char *remote)
{
	int result;
	struct argv_array cmd = ARGV_ARRAY_INIT;
	struct strbuf refmap = STRBUF_INIT;

	argv_array_pushl(&cmd, "-C", repo, "fetch", remote, NULL);
	argv_array_pushl(&cmd, "--quiet", "--prune", "--no-tags", "--refmap=", NULL);

	strbuf_addf(&refmap, "+refs/heads/*:refs/hidden/%s/*", remote);
	argv_array_push(&cmd, refmap.buf);

	result = run_command_v_opt(cmd.argv, RUN_GIT_CMD);

	strbuf_release(&refmap);
	return result;
}

static int fill_remotes(const char *repo, struct string_list *remotes)
{
	int result = 0;
	FILE *proc_out;
	struct strbuf line = STRBUF_INIT;
	struct child_process *remote_proc = xmalloc(sizeof(*remote_proc));

	child_process_init(remote_proc);

	argv_array_push(&remote_proc->args, "git");
	argv_array_push(&remote_proc->args, "-C");
	argv_array_push(&remote_proc->args, repo);
	argv_array_push(&remote_proc->args, "remote");

	remote_proc->out = -1;

	if (start_command(remote_proc)) {
		warning(_("failed to start process for repo '%s'"),
			repo);
		result = 1;
		goto cleanup;
	}

	proc_out = xfdopen(remote_proc->out, "r");

	/* if there is no line, leave the value as given */
	while (!strbuf_getline(&line, proc_out))
		string_list_append(remotes, line.buf);

	strbuf_release(&line);

	fclose(proc_out);

	if (finish_command(remote_proc)) {
		warning(_("failed to finish process for repo '%s'"),
			repo);
		result = 1;
	}
	
cleanup:
	free(remote_proc);
	return result;
}

int run_fetch_job(const char *repo)
{
	int result = 0;
	struct string_list_item *item;
	struct string_list remotes = STRING_LIST_INIT_DUP;

	if (fill_remotes(repo, &remotes)) {
		warning(_("failed to fill remotes for %s"), repo);
		result = 1;
		goto cleanup;
	}

	for (item = remotes.items;
	     item && item < remotes.items + remotes.nr;
	     item++)
		result |= fetch_remote(repo, item->string);

cleanup:
	string_list_clear(&remotes, 0);
	return result;
}
