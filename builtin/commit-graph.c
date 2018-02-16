#include "builtin.h"
#include "config.h"
#include "dir.h"
#include "lockfile.h"
#include "parse-options.h"
#include "commit-graph.h"

static char const * const builtin_commit_graph_usage[] = {
	N_("git commit-graph [--object-dir <objdir>]"),
	N_("git commit-graph read [--object-dir <objdir>] [--file=<hash>]"),
	N_("git commit-graph write [--object-dir <objdir>]"),
	NULL
};

static const char * const builtin_commit_graph_read_usage[] = {
	N_("git commit-graph read [--object-dir <objdir>] [--file=<hash>]"),
	NULL
};

static const char * const builtin_commit_graph_write_usage[] = {
	N_("git commit-graph write [--object-dir <objdir>]"),
	NULL
};

static struct opts_commit_graph {
	const char *obj_dir;
	const char *graph_file;
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

static int graph_write(int argc, const char **argv)
{
	char *graph_name;

	static struct option builtin_commit_graph_write_options[] = {
		{ OPTION_STRING, 'o', "object-dir", &opts.obj_dir,
			N_("dir"),
			N_("The object directory to store the graph") },
		OPT_END(),
	};

	argc = parse_options(argc, argv, NULL,
			     builtin_commit_graph_write_options,
			     builtin_commit_graph_write_usage, 0);

	if (!opts.obj_dir)
		opts.obj_dir = get_object_directory();

	graph_name = write_commit_graph(opts.obj_dir);

	if (graph_name) {
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

