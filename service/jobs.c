#include "git-compat-util.h"
#include "gettext.h"
#include "jobs.h"
#include "config.h"
#include "run-command.h"
#include "strbuf.h"
#include "string-list.h"

#define MAX_TIMESTAMP ((timestamp_t)-1)

struct job_list {
	struct job_description *jobs;
	size_t nr;
	size_t alloc;
};

static int run_commit_graph_job(struct job_description *job,
				const char *repo)
{
	fprintf(stderr, "COMMIT_GRAPH on %s\n", repo);
	return 0;
}

static int run_fetch_job(struct job_description *job,
			 const char *repo)
{
	fprintf(stderr, "FETCH on %s\n", repo);
	return 0;
}

static int run_loose_objects_job(struct job_description *job,
				 const char *repo)
{
	fprintf(stderr, "LOOSE_OBJECTS on %s\n", repo);
	return 0;
}

static int run_multi_pack_index_job(struct job_description *job,
				    const char *repo)
{
	fprintf(stderr, "MULTI_PACK_INDEX on %s\n", repo);
	return 0;
}

static char *config_name(const char *prefix,
			 enum job_id id,
			 const char *postfix)
{
	int postfix_dot = 1;
	struct strbuf name = STRBUF_INIT;

	if (prefix) {
		strbuf_addstr(&name, prefix);
		strbuf_addch(&name, '.');
	}

	switch (id) {
	case COMMIT_GRAPH:
		strbuf_addstr(&name, "commit-graph");
		break;

	case FETCH:
		strbuf_addstr(&name, "fetch");
		break;

	case LOOSE_OBJECTS:
		strbuf_addstr(&name, "loose-objects");
		break;

	case MULTI_PACK_INDEX:
		strbuf_addstr(&name, "multi-pack-index");
		break;

	default:
		postfix_dot = 0;
		break;
	}

	if (postfix) {
		if (postfix_dot)
			strbuf_addch(&name, '.');
		strbuf_addstr(&name, postfix);
	}

	return strbuf_detach(&name, NULL);
}

static int try_get_timestamp(enum job_id id,
			     const char *repo,
			     const char *postfix,
			     timestamp_t *t)
{
	int result = 0;
	FILE *proc_out;
	struct strbuf last_run_line = STRBUF_INIT;
	char *cname = config_name("job", id, postfix);
	struct child_process *config_proc = xmalloc(sizeof(*config_proc));

	child_process_init(config_proc);

	argv_array_push(&config_proc->args, "git");
	argv_array_push(&config_proc->args, "-C");
	argv_array_push(&config_proc->args, repo);
	argv_array_push(&config_proc->args, "config");
	argv_array_push(&config_proc->args, cname);
	free(cname);

	config_proc->out = -1;

	if (start_command(config_proc)) {
		warning(_("failed to start process for repo '%s'"),
			repo);
		result = 1;
		goto cleanup;
	}

	proc_out = xfdopen(config_proc->out, "r");

	/* if there is no line, leave the value as given */
	if (!strbuf_getline(&last_run_line, proc_out)) {
		*t = atol(last_run_line.buf);
		strbuf_release(&last_run_line);
	}

	fclose(proc_out);

	if (finish_command(config_proc)) {
		warning(_("failed to finish process for repo '%s'"),
			repo);
		result = 1;
	}
	
cleanup:
	free(config_proc);
	return result;
}

static timestamp_t get_last_run(enum job_id id,
				const char *repo)
{
	timestamp_t last_run = 0;

	/* In an error state, do not run the job */
	if (try_get_timestamp(id, repo, "lastrun", &last_run))
		return MAX_TIMESTAMP;

	return last_run;
}

static timestamp_t get_interval(enum job_id id,
				const char *repo)
{
	timestamp_t interval = MAX_TIMESTAMP;

	/* In an error state, do not run the job */
	if (try_get_timestamp(id, repo, "interval", &interval))
		return MAX_TIMESTAMP;
	
	return interval;
}

static int set_last_run(enum job_id id,
			const char *repo,
			timestamp_t last_run)
{
	int result = 0;
	struct strbuf last_run_string = STRBUF_INIT;
	char *cname = config_name("job", id, "lastrun");

	strbuf_addf(&last_run_string, "%"PRItime, last_run);

	struct child_process *config_proc = xmalloc(sizeof(*config_proc));

	child_process_init(config_proc);

	argv_array_push(&config_proc->args, "git");
	argv_array_push(&config_proc->args, "-C");
	argv_array_push(&config_proc->args, repo);
	argv_array_push(&config_proc->args, "config");
	argv_array_push(&config_proc->args, cname);
	argv_array_push(&config_proc->args, last_run_string.buf);
	free(cname);
	strbuf_release(&last_run_string);

	if (start_command(config_proc)) {
		warning(_("failed to start process for repo '%s'"),
			repo);
		result = 1;
		goto cleanup;
	}

	if (finish_command(config_proc)) {
		warning(_("failed to finish process for repo '%s'"),
			repo);
		result = 1;
	}
	
cleanup:
	free(config_proc);
	return result;
}

static int run_job(struct job_description *job,
		   const char *repo)
{
	int result = 0;
	timestamp_t now = time(NULL);
	timestamp_t last_run = get_last_run(job->id, repo);
	timestamp_t interval = get_interval(job->id, repo);

	if (last_run + interval > now) {
		fprintf(stderr, "not ready for job %d in repo %s (%"PRItime" + %"PRItime" > %"PRItime"\n",
				job->id, repo, last_run, interval, now);
		return 0;
	}

	switch (job->id) {
	case COMMIT_GRAPH:
		result = run_commit_graph_job(job, repo);
		break;

	case FETCH:
		result = run_fetch_job(job, repo);
		break;

	case LOOSE_OBJECTS:
		result = run_loose_objects_job(job, repo);
		break;

	case MULTI_PACK_INDEX:
		result = run_multi_pack_index_job(job, repo);
		break;

	default:
		error(_("unknown job type %d"), job->id);
		result = 1;
		break;
	}

	set_last_run(job->id, repo, now);

	return result;
}

static int load_active_repos(struct string_list *repos)
{
	struct string_list_item *item;
	const struct string_list *config_repos = git_config_get_value_multi("job.repo");

	for (item = config_repos->items;
	     item && item < config_repos->items + config_repos->nr;
	     item++) {
		DIR *dir = opendir(item->string);

		if (!dir)
			continue;

		closedir(dir);
		string_list_append(repos, item->string);
	}

	string_list_sort(repos);
	string_list_remove_duplicates(repos, 0);

	return 0;
}

static int run_job_loop_step(struct job_list *list)
{
	int i;
	int result = 0;
	struct string_list repos = STRING_LIST_INIT_DUP;

	if ((result = load_active_repos(&repos)))
		return result;

	for (i = 0; !result && i < list->nr; i++) {
		struct string_list_item *item;
		for (item = repos.items;
		     !result && item && item < repos.items + repos.nr;
		     item++)
			result = run_job(&list->jobs[i],
					 item->string);
	}

	string_list_clear(&repos, 0);
	return result;
}

static void insert_job_type(struct job_list *list,
			    enum job_id id)
{
	/*
	 * TODO: check config for possible jobs to run
	 * or not run. Check config for interval. Perhaps
	 * add a "flags" option to the job_description struct
	 * for translating advanced options.
	 */
	list->jobs[list->nr].id = id;
	list->nr++;
}

static int initialize_jobs(struct job_list *list)
{
	memset(list, 0, sizeof(*list));

	list->alloc = MAX_JOB_COUNT;
	ALLOC_ARRAY(list->jobs, list->alloc);

	insert_job_type(list, COMMIT_GRAPH);
	insert_job_type(list, FETCH);
	insert_job_type(list, LOOSE_OBJECTS);
	insert_job_type(list, MULTI_PACK_INDEX);

	return 0;
}

unsigned int get_loop_interval(void)
{
	timestamp_t interval = 60;
	
	try_get_timestamp(NONE, ".", "loopinterval", &interval);

	return interval;
}

int setup_and_run_job_loop(void)
{
	int result;
	struct job_list list;

	result = initialize_jobs(&list);

	while (!(result = run_job_loop_step(&list))) {
		unsigned int interval = get_loop_interval();
		sleep(interval);
	}

	return result;
}
