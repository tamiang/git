#include "builtin.h"
#include "config.h"
#include "dir.h"
#include "parse-options.h"
#include "pathspec.h"
#include "repository.h"
#include "run-command.h"
#include "strbuf.h"
#include "string-list.h"

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
	struct argv_array cone_argv = ARGV_ARRAY_INIT;
	
	if (git_config_set_gently("extensions.worktreeConfig", "true")) {
		error(_("failed to set extensions.worktreeConfig setting"));
		return 1;
	}

	argv_array_pushl(&argv, "config", "--worktree", "core.sparseCheckout", NULL);

	switch (mode) {
	case SPARSE_CHECKOUT_FULL:
	case SPARSE_CHECKOUT_CONE:
		argv_array_pushl(&argv, "true", NULL);
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

	argv_array_pushl(&cone_argv, "config", "--worktree",
			 "core.sparseCheckoutCone", NULL);

	if (mode == SPARSE_CHECKOUT_CONE)
		argv_array_push(&cone_argv, "true");
	else	
		argv_array_push(&cone_argv, "false");

	if (run_command_v_opt(cone_argv.argv, RUN_GIT_CMD)) {
		error(_("failed to enable core.sparseCheckoutCone"));
		return 1;
	}
	
	return 0;
}

static char const * const builtin_sparse_checkout_init_usage[] = {
	N_("git sparse-checkout init [--cone]"),
	NULL
};

static struct sparse_checkout_init_opts {
	int cone_mode;
} init_opts;

static int sparse_checkout_init(int argc, const char **argv)
{
	struct pattern_list pl;
	char *sparse_filename;
	FILE *fp;
	int res;
	struct object_id oid;
	int mode;

	static struct option builtin_sparse_checkout_init_options[] = {
		OPT_BOOL(0, "cone", &init_opts.cone_mode,
			 N_("initialize the sparse-checkout in cone mode")),
		OPT_END(),
	};

	argc = parse_options(argc, argv, NULL,
			     builtin_sparse_checkout_init_options,
			     builtin_sparse_checkout_init_usage, 0);

	mode = init_opts.cone_mode ? SPARSE_CHECKOUT_CONE : SPARSE_CHECKOUT_FULL;

	if (sc_set_config(mode))
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

static void insert_recursive_pattern(struct pattern_list *pl, struct strbuf *path)
{
	struct pattern_entry *e = xmalloc(sizeof(struct pattern_entry));
	e->patternlen = path->len;
	e->pattern = strbuf_detach(path, NULL);
	hashmap_entry_init(e, memhash(e->pattern, e->patternlen));

	hashmap_add(&pl->recursive_hashmap, e);

	while (e->patternlen) {
		char *slash = strrchr(e->pattern, '/');
		char *oldpattern = e->pattern;
		size_t newlen;

		if (!slash)
			break;

		newlen = slash - e->pattern;
		e = xmalloc(sizeof(struct pattern_entry));
		e->patternlen = newlen;
		e->pattern = xstrndup(oldpattern, newlen);
		hashmap_entry_init(e, memhash(e->pattern, e->patternlen));

		if (!hashmap_get(&pl->parent_hashmap, e, NULL))
			hashmap_add(&pl->parent_hashmap, e);
	}
}

static void write_cone_to_file(FILE *fp, struct pattern_list *pl)
{
	int i;
	struct pattern_entry *entry;
	struct hashmap_iter iter;
	struct string_list sl = STRING_LIST_INIT_DUP;

	hashmap_iter_init(&pl->parent_hashmap, &iter);
	while ((entry = hashmap_iter_next(&iter))) {
		char *pattern = xstrdup(entry->pattern);
		char *converted = pattern;
		if (pattern[0] == '/')
			converted++;
		if (pattern[entry->patternlen - 1] == '/')
			pattern[entry->patternlen - 1] = 0;
		string_list_insert(&sl, converted);
		free(pattern);
	}

	string_list_sort(&sl);
	string_list_remove_duplicates(&sl, 0);

	fprintf(fp, "/*\n!/*/*\n");

	for (i = 0; i < sl.nr; i++) {
		char *pattern = sl.items[i].string;

		if (strlen(pattern))
			fprintf(fp, "/%s/*\n!/%s/*/*\n", pattern, pattern);
	}

	string_list_clear(&sl, 0);

	hashmap_iter_init(&pl->recursive_hashmap, &iter);
	while ((entry = hashmap_iter_next(&iter))) {
		char *pattern = xstrdup(entry->pattern);
		char *converted = pattern;
		if (pattern[0] == '/')
			converted++;
		if (pattern[entry->patternlen - 1] == '/')
			pattern[entry->patternlen - 1] = 0;
		string_list_insert(&sl, converted);
		free(pattern);
	}

	string_list_sort(&sl);
	string_list_remove_duplicates(&sl, 0);

	for (i = 0; i < sl.nr; i++) {
		char *pattern = sl.items[i].string;
		fprintf(fp, "/%s/*\n", pattern);
	}
}

static int write_patterns_and_update(struct pattern_list *pl)
{
	char *sparse_filename;
	FILE *fp;

	sparse_filename = get_sparse_checkout_filename();
	fp = fopen(sparse_filename, "w");

	if (core_sparse_checkout_cone)
		write_cone_to_file(fp, pl);
	else
		write_patterns_to_file(fp, pl);

	fclose(fp);
	free(sparse_filename);

	clear_pattern_list(pl);
	return update_working_directory();
}

static void strbuf_to_cone_pattern(struct strbuf *line, struct pattern_list *pl)
{
	strbuf_trim(line);

	strbuf_trim_trailing_dir_sep(line);

	if (!line->len)
		return;

	if (line->buf[0] == '/')
		strbuf_remove(line, 0, 1);

	if (!line->len)
		return;

	insert_recursive_pattern(pl, line);
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

	if (core_sparse_checkout_cone) {
		struct strbuf line = STRBUF_INIT;
		hashmap_init(&pl.recursive_hashmap, pl_hashmap_cmp, NULL, 0);
		hashmap_init(&pl.parent_hashmap, pl_hashmap_cmp, NULL, 0);

		if (set_opts.use_stdin) {
			while (!strbuf_getline(&line, stdin))
				strbuf_to_cone_pattern(&line, &pl);
		} else {
			for (i = 0; i < argc; i++) {
				strbuf_setlen(&line, 0);
				strbuf_addstr(&line, argv[i]);
				strbuf_to_cone_pattern(&line, &pl);
			}
		}
	} else {
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
