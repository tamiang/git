#include "builtin.h"
#include "config.h"
#include "dir.h"
#include "lockfile.h"
#include "parse-options.h"
#include "commit-graph.h"

static char const * const builtin_commit_graph_usage[] = {
	N_("git commit-graph [--object-dir <objdir>]"),
	N_("git commit-graph read [--object-dir <objdir>] [--file=<hash>]"),
	N_("git commit-graph write [--object-dir <objdir>] [--set-latest] [--delete-expired] [--stdin-packs|--stdin-commits]"),
	NULL
};

static const char * const builtin_commit_graph_read_usage[] = {
	N_("git commit-graph read [--object-dir <objdir>] [--file=<hash>]"),
	NULL
};

static const char * const builtin_commit_graph_write_usage[] = {
	N_("git commit-graph write [--object-dir <objdir>] [--set-latest] [--delete-expired] [--stdin-packs|--stdin-commits]"),
	NULL
};

static struct opts_commit_graph {
	const char *obj_dir;
	const char *graph_file;
	int set_latest;
	int delete_expired;
	int stdin_packs;
	int stdin_commits;
} opts;

static int graph_read(int argc, const char **argv)
{
	struct commit_graph *graph = 0;
	struct strbuf full_path = STRBUF_INIT;

	static struct option builtin_commit_graph_read_options[] = {
		{ OPTION_STRING, 'o', "object-dir", &opts.obj_dir,
			N_("dir"),
			N_("The object directory to store the graph") },
		{ OPTION_STRING, 'H', "file", &opts.graph_file,
			N_("file"),
			N_("The filename for a specific commit graph file in the object directory."),
			PARSE_OPT_OPTARG, NULL, (intptr_t) "" },
		OPT_END(),
	};

	argc = parse_options(argc, argv, NULL,
			     builtin_commit_graph_read_options,
			     builtin_commit_graph_read_usage, 0);

	if (!opts.obj_dir)
		opts.obj_dir = get_object_directory();

	if (!opts.graph_file)
		die("no graph hash specified");

	strbuf_addf(&full_path, "%s/info/%s", opts.obj_dir, opts.graph_file);
	graph = load_commit_graph_one(full_path.buf);

	if (!graph)
		die("graph file %s does not exist", full_path.buf);

	printf("header: %08x %d %d %d %d\n",
		ntohl(*(uint32_t*)graph->data),
		*(unsigned char*)(graph->data + 4),
		*(unsigned char*)(graph->data + 5),
		*(unsigned char*)(graph->data + 6),
		*(unsigned char*)(graph->data + 7));
	printf("num_commits: %u\n", graph->num_commits);
	printf("chunks:");

	if (graph->chunk_oid_fanout)
		printf(" oid_fanout");
	if (graph->chunk_oid_lookup)
		printf(" oid_lookup");
	if (graph->chunk_commit_data)
		printf(" commit_metadata");
	if (graph->chunk_large_edges)
		printf(" large_edges");
	printf("\n");

	return 0;
}

static void set_latest_file(const char *obj_dir, const char *graph_file)
{
	int fd;
	struct lock_file lk = LOCK_INIT;
	char *latest_fname = get_graph_latest_filename(obj_dir);

	fd = hold_lock_file_for_update(&lk, latest_fname, LOCK_DIE_ON_ERROR);
	FREE_AND_NULL(latest_fname);

	if (fd < 0)
		die_errno("unable to open graph-head");

	write_in_full(fd, graph_file, strlen(graph_file));
	commit_lock_file(&lk);
}

/*
 * To avoid race conditions and deleting graph files that are being
 * used by other processes, look inside a pack directory for all files
 * of the form "graph-<hash>.graph" that do not match the old or new
 * graph hashes and delete them.
 */
static void do_delete_expired(const char *obj_dir,
			      const char *old_graph_name,
			      const char *new_graph_name)
{
	DIR *dir;
	struct dirent *de;
	int dirnamelen;
	struct strbuf path = STRBUF_INIT;

	strbuf_addf(&path, "%s/info", obj_dir);
	dir = opendir(path.buf);
	if (!dir) {
		if (errno != ENOENT)
			error_errno("unable to open object pack directory: %s",
				    obj_dir);
		return;
	}

	strbuf_addch(&path, '/');
	dirnamelen = path.len;
	while ((de = readdir(dir)) != NULL) {
		size_t base_len;

		if (is_dot_or_dotdot(de->d_name))
			continue;

		strbuf_setlen(&path, dirnamelen);
		strbuf_addstr(&path, de->d_name);

		base_len = path.len;
		if (strip_suffix_mem(path.buf, &base_len, ".graph") &&
		    strcmp(new_graph_name, de->d_name) &&
		    (!old_graph_name || strcmp(old_graph_name, de->d_name)) &&
		    remove_path(path.buf))
			die("failed to remove path %s", path.buf);
	}

	strbuf_release(&path);
}

static int graph_write(int argc, const char **argv)
{
	char *graph_name;
	char *old_graph_name;
	const char **pack_indexes = NULL;
	int nr_packs = 0;
	const char **commit_hex = NULL;
	int nr_commits = 0;
	const char **lines = NULL;
	int nr_lines = 0;
	int alloc_lines = 0;

	static struct option builtin_commit_graph_write_options[] = {
		{ OPTION_STRING, 'o', "object-dir", &opts.obj_dir,
			N_("dir"),
			N_("The object directory to store the graph") },
		OPT_BOOL('u', "set-latest", &opts.set_latest,
			N_("update graph-head to written graph file")),
		OPT_BOOL('d', "delete-expired", &opts.delete_expired,
			N_("delete expired head graph file")),
		OPT_BOOL('s', "stdin-packs", &opts.stdin_packs,
			N_("only scan packfiles listed by stdin")),
		OPT_BOOL('C', "stdin-commits", &opts.stdin_commits,
			N_("start walk at commits listed by stdin")),
		OPT_END(),
	};

	argc = parse_options(argc, argv, NULL,
			     builtin_commit_graph_write_options,
			     builtin_commit_graph_write_usage, 0);

	if (opts.stdin_packs && opts.stdin_commits)
		die(_("cannot use both --stdin-commits and --stdin-packs"));
	if (!opts.obj_dir)
		opts.obj_dir = get_object_directory();

	old_graph_name = get_graph_latest_contents(opts.obj_dir);

	if (opts.stdin_packs || opts.stdin_commits) {
		struct strbuf buf = STRBUF_INIT;
		nr_lines = 0;
		alloc_lines = 128;
		ALLOC_ARRAY(lines, alloc_lines);

		while (strbuf_getline(&buf, stdin) != EOF) {
			ALLOC_GROW(lines, nr_lines + 1, alloc_lines);
			lines[nr_lines++] = buf.buf;
			strbuf_detach(&buf, NULL);
		}

		if (opts.stdin_packs) {
			pack_indexes = lines;
			nr_packs = nr_lines;
		}
		if (opts.stdin_commits) {
			commit_hex = lines;
			nr_commits = nr_lines;
		}
	}

	graph_name = write_commit_graph(opts.obj_dir,
					pack_indexes,
					nr_packs,
					commit_hex,
					nr_commits);

	if (graph_name) {
		if (opts.set_latest)
			set_latest_file(opts.obj_dir, graph_name);

		if (opts.delete_expired)
			do_delete_expired(opts.obj_dir,
					  old_graph_name,
					  graph_name);

		printf("%s\n", graph_name);
		FREE_AND_NULL(graph_name);
	}

	return 0;
}

int cmd_commit_graph(int argc, const char **argv, const char *prefix)
{
	static struct option builtin_commit_graph_options[] = {
		{ OPTION_STRING, 'o', "object-dir", &opts.obj_dir,
			N_("dir"),
			N_("The object directory to store the graph") },
		OPT_END(),
	};

	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage_with_options(builtin_commit_graph_usage,
				   builtin_commit_graph_options);

	git_config(git_default_config, NULL);
	argc = parse_options(argc, argv, prefix,
			     builtin_commit_graph_options,
			     builtin_commit_graph_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);

	if (argc > 0) {
		if (!strcmp(argv[0], "read"))
			return graph_read(argc, argv);
		if (!strcmp(argv[0], "write"))
			return graph_write(argc, argv);
	}

	usage_with_options(builtin_commit_graph_usage,
			   builtin_commit_graph_options);
}

