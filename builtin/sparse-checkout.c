#include "builtin.h"
#include "config.h"
#include "dir.h"
#include "parse-options.h"
#include "pathspec.h"
#include "repository.h"
#include "run-command.h"
#include "strbuf.h"

static char const * const builtin_sparse_checkout_usage[] = {
	N_("git sparse-checkout [init|list|set|disable] <options>"),
	NULL
};

static char *get_sparse_checkout_filename(void)
{
	return git_pathdup("info/sparse-checkout");
}

static void write_patterns_to_file(FILE *fp, struct pattern_list *pl)
{
	int i;

	for (i = 0; i < pl->nr; i++) {
		struct path_pattern *p = pl->patterns[i];

		if (p->flags & PATTERN_FLAG_NEGATIVE)
			fprintf(fp, "!");

		fprintf(fp, "%s", p->pattern);

		if (p->flags & PATTERN_FLAG_MUSTBEDIR)
			fprintf(fp, "/");

		fprintf(fp, "\n");
	}
}

static int sparse_checkout_list(int argc, const char **argv)
{
	struct pattern_list pl;
	char *sparse_filename;
	int res;

	memset(&pl, 0, sizeof(pl));

	sparse_filename = get_sparse_checkout_filename();
	res = add_patterns_from_file_to_list(sparse_filename, "", 0, &pl, NULL);
	free(sparse_filename);

	if (res < 0) {
		warning(_("this worktree is not sparse (sparse-checkout file may not exist)"));
		return 0;
	}

	write_patterns_to_file(stdout, &pl);
	clear_pattern_list(&pl);

	return 0;
}

static int update_working_directory(void)
{
	struct argv_array argv = ARGV_ARRAY_INIT;
	int result = 0;
	argv_array_pushl(&argv, "read-tree", "-m", "-u", "HEAD", NULL);

	if (run_command_v_opt(argv.argv, RUN_GIT_CMD)) {
		error(_("failed to update index with new sparse-checkout paths"));
		result = 1;
	}

	argv_array_clear(&argv);
	return result;
}

#define SPARSE_CHECKOUT_NONE 0
#define SPARSE_CHECKOUT_FULL 1
#define SPARSE_CHECKOUT_CONE 2

static int sc_set_config(int mode)
{
	struct argv_array argv = ARGV_ARRAY_INIT;
	
	if (git_config_set_gently("extensions.worktreeConfig", "true")) {
		error(_("failed to set extensions.worktreeConfig setting"));
		return 1;
	}

	argv_array_pushl(&argv, "config", "--worktree", "core.sparseCheckout", NULL);

	switch (mode) {
	case SPARSE_CHECKOUT_FULL:
		argv_array_pushl(&argv, "true", NULL);
		break;

	case SPARSE_CHECKOUT_CONE:
		argv_array_pushl(&argv, "cone", NULL);
		break;

	case SPARSE_CHECKOUT_NONE:
		argv_array_pushl(&argv, "false", NULL);
		break;

	default:
		die(_("invalid config mode"));
	}

	if (run_command_v_opt(argv.argv, RUN_GIT_CMD)) {
		error(_("failed to enable core.sparseCheckout"));
		return 1;
	}

	return 0;
}

static int sparse_checkout_init(int argc, const char **argv)
{
	struct pattern_list pl;
	char *sparse_filename;
	FILE *fp;
	int res;
	struct object_id oid;

	if (sc_set_config(SPARSE_CHECKOUT_FULL))
		return 1;

	memset(&pl, 0, sizeof(pl));

	sparse_filename = get_sparse_checkout_filename();
	res = add_patterns_from_file_to_list(sparse_filename, "", 0, &pl, NULL);

	/* If we already have a sparse-checkout file, use it. */
	if (res >= 0) {
		free(sparse_filename);
		goto reset_dir;
	}

	/* initial mode: all blobs at root */
	fp = fopen(sparse_filename, "w");
	free(sparse_filename);
	fprintf(fp, "/*\n!/*/*\n");
	fclose(fp);

	if (get_oid("HEAD", &oid)) {
		/* assume we are in a fresh repo */
		return 0;
	}

reset_dir:
	return update_working_directory();
}

static int write_patterns_and_update(struct pattern_list *pl)
{
	char *sparse_filename;
	FILE *fp;
	struct strbuf line = STRBUF_INIT;

	sparse_filename = get_sparse_checkout_filename();
	fp = fopen(sparse_filename, "w");
	write_patterns_to_file(fp, pl);

	while (!strbuf_getline(&line, stdin)) {
		strbuf_trim(&line);
		fprintf(fp, "%s\n", line.buf);
	}

	fclose(fp);
	free(sparse_filename);

	clear_pattern_list(pl);
	return update_working_directory();
}

static char const * const builtin_sparse_checkout_set_usage[] = {
	N_("git sparse-checkout set [--stdin|<patterns>]"),
	NULL
};

static struct sparse_checkout_set_opts {
	int use_stdin;
} set_opts;

static int sparse_checkout_set(int argc, const char **argv, const char *prefix)
{
	int i;
	struct pattern_list pl;

	static struct option builtin_sparse_checkout_set_options[] = {
		OPT_BOOL(0, "stdin", &set_opts.use_stdin,
			 N_("read patterns from standard in")),
		OPT_END(),
	};

	memset(&pl, 0, sizeof(pl));

	argc = parse_options(argc, argv, prefix,
			     builtin_sparse_checkout_set_options,
			     builtin_sparse_checkout_set_usage,
			     PARSE_OPT_KEEP_UNKNOWN);

	if (set_opts.use_stdin) {
		struct strbuf line = STRBUF_INIT;

		while (!strbuf_getline(&line, stdin)) {
			size_t len;
			char *buf = strbuf_detach(&line, &len);
			add_pattern(buf, buf, len, &pl, 0);
		}
	} else {
		for (i = 0; i < argc; i++)
			add_pattern(argv[i], argv[i], strlen(argv[i]), &pl, 0);
	}

	return write_patterns_and_update(&pl);
}

static int sparse_checkout_disable(int argc, const char **argv)
{
	char *sparse_filename;
	FILE *fp;

	if (sc_set_config(SPARSE_CHECKOUT_FULL))
		die(_("failed to change config"));

	sparse_filename = get_sparse_checkout_filename();
	fp = fopen(sparse_filename, "w");
	fprintf(fp, "/*\n");
	fclose(fp);

	if (update_working_directory())
		die(_("error while refreshing working directory"));

	unlink(sparse_filename);
	free(sparse_filename);

	return sc_set_config(SPARSE_CHECKOUT_NONE);
}

int cmd_sparse_checkout(int argc, const char **argv, const char *prefix)
{
	static struct option builtin_sparse_checkout_options[] = {
		OPT_END(),
	};

	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage_with_options(builtin_sparse_checkout_usage,
				   builtin_sparse_checkout_options);

	argc = parse_options(argc, argv, prefix,
			     builtin_sparse_checkout_options,
			     builtin_sparse_checkout_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);

	git_config(git_default_config, NULL);

	if (argc > 0) {
		if (!strcmp(argv[0], "list"))
			return sparse_checkout_list(argc, argv);
		if (!strcmp(argv[0], "init"))
			return sparse_checkout_init(argc, argv);
		if (!strcmp(argv[0], "set"))
			return sparse_checkout_set(argc, argv, prefix);
		if (!strcmp(argv[0], "disable"))
			return sparse_checkout_disable(argc, argv);
	}

	usage_with_options(builtin_sparse_checkout_usage,
			   builtin_sparse_checkout_options);
}
