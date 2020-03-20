#include "builtin.h"
#include "config.h"
#include "dir.h"
#include "parse-options.h"
#include "pathspec.h"
#include "repository.h"
#include "run-command.h"
#include "strbuf.h"
#include "string-list.h"
#include "cache.h"
#include "cache-tree.h"
#include "resolve-undo.h"
#include "unpack-trees.h"
#include "wt-status.h"
#include "quote.h"
#include "sparse-checkout.h"

static const char *empty_base = "";

static char const * const builtin_sparse_checkout_usage[] = {
	N_("git sparse-checkout (init|list|set|add|reapply|disable) <options>"),
	NULL
};

static int sparse_checkout_list(int argc, const char **argv)
{
	struct pattern_list pl;
	char *sparse_filename;
	int res;

	memset(&pl, 0, sizeof(pl));

	pl.use_cone_patterns = core_sparse_checkout_cone;

	sparse_filename = get_sparse_checkout_filename();
	res = add_patterns_from_file_to_list(sparse_filename, "", 0, &pl, NULL);
	free(sparse_filename);

	if (res < 0) {
		warning(_("this worktree is not sparse (sparse-checkout file may not exist)"));
		return 0;
	}

	if (pl.use_cone_patterns) {
		int i;
		struct pattern_entry *pe;
		struct hashmap_iter iter;
		struct string_list sl = STRING_LIST_INIT_DUP;

		hashmap_for_each_entry(&pl.recursive_hashmap, &iter, pe, ent) {
			/* pe->pattern starts with "/", skip it */
			string_list_insert(&sl, pe->pattern + 1);
		}

		string_list_sort(&sl);

		for (i = 0; i < sl.nr; i++) {
			quote_c_style(sl.items[i].string, NULL, stdout, 0);
			printf("\n");
		}

		return 0;
	}

	write_patterns_to_file(stdout, &pl);
	clear_pattern_list(&pl);

	return 0;
}

enum sparse_checkout_mode {
	MODE_NO_PATTERNS = 0,
	MODE_ALL_PATTERNS = 1,
	MODE_CONE_PATTERNS = 2,
};

static int set_config(enum sparse_checkout_mode mode)
{
	const char *config_path;

	if (git_config_set_gently("extensions.worktreeConfig", "true")) {
		error(_("failed to set extensions.worktreeConfig setting"));
		return 1;
	}

	config_path = git_path("config.worktree");
	git_config_set_in_file_gently(config_path,
				      "core.sparseCheckout",
				      mode ? "true" : NULL);

	git_config_set_in_file_gently(config_path,
				      "core.sparseCheckoutCone",
				      mode == MODE_CONE_PATTERNS ? "true" : NULL);

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
	int res;
	struct object_id oid;
	int mode;
	struct strbuf pattern = STRBUF_INIT;

	static struct option builtin_sparse_checkout_init_options[] = {
		OPT_BOOL(0, "cone", &init_opts.cone_mode,
			 N_("initialize the sparse-checkout in cone mode")),
		OPT_END(),
	};

	repo_read_index(the_repository);

	argc = parse_options(argc, argv, NULL,
			     builtin_sparse_checkout_init_options,
			     builtin_sparse_checkout_init_usage, 0);

	if (init_opts.cone_mode) {
		mode = MODE_CONE_PATTERNS;
		core_sparse_checkout_cone = 1;
	} else
		mode = MODE_ALL_PATTERNS;

	if (set_config(mode))
		return 1;

	memset(&pl, 0, sizeof(pl));

	sparse_filename = get_sparse_checkout_filename();
	res = add_patterns_from_file_to_list(sparse_filename, "", 0, &pl, NULL);

	/* If we already have a sparse-checkout file, use it. */
	if (res >= 0) {
		free(sparse_filename);
		core_apply_sparse_checkout = 1;
		return update_working_directory(NULL);
	}

	if (get_oid("HEAD", &oid)) {
		FILE *fp;

		/* assume we are in a fresh repo, but update the sparse-checkout file */
		fp = xfopen(sparse_filename, "w");
		if (!fp)
			die(_("failed to open '%s'"), sparse_filename);

		free(sparse_filename);
		fprintf(fp, "/*\n!/*/\n");
		fclose(fp);
		return 0;
	}

	strbuf_addstr(&pattern, "/*");
	add_pattern(strbuf_detach(&pattern, NULL), empty_base, 0, &pl, 0);
	strbuf_addstr(&pattern, "!/*/");
	add_pattern(strbuf_detach(&pattern, NULL), empty_base, 0, &pl, 0);

	return write_patterns_and_update(&pl);
}

static char const * const builtin_sparse_checkout_set_usage[] = {
	N_("git sparse-checkout (set|add) (--stdin | <patterns>)"),
	NULL
};

static struct sparse_checkout_set_opts {
	int use_stdin;
	int in_tree;
} set_opts;

static void add_patterns_from_input(struct pattern_list *pl,
				    int argc, const char **argv)
{
	int i;
	if (core_sparse_checkout_cone) {
		struct strbuf line = STRBUF_INIT;

		hashmap_init(&pl->recursive_hashmap, pl_hashmap_cmp, NULL, 0);
		hashmap_init(&pl->parent_hashmap, pl_hashmap_cmp, NULL, 0);
		pl->use_cone_patterns = 1;

		if (set_opts.use_stdin) {
			struct strbuf unquoted = STRBUF_INIT;
			while (!strbuf_getline(&line, stdin)) {
				if (line.buf[0] == '"') {
					strbuf_reset(&unquoted);
					if (unquote_c_style(&unquoted, line.buf, NULL))
						die(_("unable to unquote C-style string '%s'"),
						line.buf);

					strbuf_swap(&unquoted, &line);
				}

				strbuf_to_cone_pattern(&line, pl);
			}

			strbuf_release(&unquoted);
		} else {
			for (i = 0; i < argc; i++) {
				strbuf_setlen(&line, 0);
				strbuf_addstr(&line, argv[i]);
				strbuf_to_cone_pattern(&line, pl);
			}
		}
	} else {
		if (set_opts.use_stdin) {
			struct strbuf line = STRBUF_INIT;

			while (!strbuf_getline(&line, stdin)) {
				size_t len;
				char *buf = strbuf_detach(&line, &len);
				add_pattern(buf, empty_base, 0, pl, 0);
			}
		} else {
			for (i = 0; i < argc; i++)
				add_pattern(argv[i], empty_base, 0, pl, 0);
		}
	}
}

enum modify_type {
	REPLACE,
	ADD,
};

static void add_patterns_cone_mode(int argc, const char **argv,
				   struct pattern_list *pl)
{
	struct strbuf buffer = STRBUF_INIT;
	struct pattern_entry *pe;
	struct hashmap_iter iter;
	struct pattern_list existing;

	add_patterns_from_input(pl, argc, argv);

	if (load_sparse_checkout_patterns(&existing))
		die(_("unable to load existing sparse-checkout patterns"));

	hashmap_for_each_entry(&existing.recursive_hashmap, &iter, pe, ent) {
		if (!hashmap_contains_parent(&pl->recursive_hashmap,
					pe->pattern, &buffer) ||
		    !hashmap_contains_parent(&pl->parent_hashmap,
					pe->pattern, &buffer)) {
			strbuf_reset(&buffer);
			strbuf_addstr(&buffer, pe->pattern);
			insert_recursive_pattern(pl, &buffer);
		}
	}

	clear_pattern_list(&existing);
	strbuf_release(&buffer);
}

static void add_patterns_literal(int argc, const char **argv,
				 struct pattern_list *pl)
{
	char *sparse_filename = get_sparse_checkout_filename();
	if (add_patterns_from_file_to_list(sparse_filename, "", 0,
					   pl, NULL))
		die(_("unable to load existing sparse-checkout patterns"));
	free(sparse_filename);
	add_patterns_from_input(pl, argc, argv);
}

static int modify_pattern_list(int argc, const char **argv, enum modify_type m)
{
	int result;
	int changed_config = 0;
	struct pattern_list pl;
	memset(&pl, 0, sizeof(pl));

	switch (m) {
	case ADD:
		if (core_sparse_checkout_cone)
			add_patterns_cone_mode(argc, argv, &pl);
		else
			add_patterns_literal(argc, argv, &pl);
		break;

	case REPLACE:
		add_patterns_from_input(&pl, argc, argv);
		break;
	}

	if (!core_apply_sparse_checkout) {
		set_config(MODE_ALL_PATTERNS);
		core_apply_sparse_checkout = 1;
		changed_config = 1;
	}

	result = write_patterns_and_update(&pl);

	if (result && changed_config)
		set_config(MODE_NO_PATTERNS);

	clear_pattern_list(&pl);
	return result;
}

static int modify_in_tree_list(int argc, const char **argv, enum modify_type m)
{
	int result = 0;
	int i;
	struct string_list sl = STRING_LIST_INIT_DUP;
	struct pattern_list pl;

	switch(m) {
	case ADD:
		if (load_in_tree_from_config(the_repository, &sl))
			return 1;
		if (!sl.nr)
			warning(_("the existing in-tree config has no entries; this overwriting existing sparse-checkout definition."));
		load_sparse_checkout_patterns(&pl);
		break;
		
	case REPLACE:
		memset(&pl, 0, sizeof(pl));
		hashmap_init(&pl.recursive_hashmap, pl_hashmap_cmp, NULL, 0);
		hashmap_init(&pl.parent_hashmap, pl_hashmap_cmp, NULL, 0);
		break;
	}

	for (i = 0; i < argc; i++)
		string_list_insert(&sl, argv[i]);

	if (load_in_tree_pattern_list(the_repository->index, &sl, &pl) ||
	    write_patterns_and_update(&pl) ||
	    set_in_tree_config(the_repository, &sl))
		result = 1;

	string_list_clear(&sl, 0);
	clear_pattern_list(&pl);

	return result;
}

static int sparse_checkout_set(int argc, const char **argv, const char *prefix,
			       enum modify_type m)
{
	static struct option builtin_sparse_checkout_set_options[] = {
		OPT_BOOL(0, "stdin", &set_opts.use_stdin,
			 N_("read patterns from standard in")),
		OPT_BOOL_F(0, "in-tree", &set_opts.in_tree,
			 N_("use paths in the tree to specify lists of directories"),
			 PARSE_OPT_NONEG),
		OPT_END(),
	};

	repo_read_index(the_repository);

	argc = parse_options(argc, argv, prefix,
			     builtin_sparse_checkout_set_options,
			     builtin_sparse_checkout_set_usage,
			     PARSE_OPT_KEEP_UNKNOWN);

	if (set_opts.in_tree)
		return modify_in_tree_list(argc, argv, m);
	return modify_pattern_list(argc, argv, m);
}

static int sparse_checkout_reapply(int argc, const char **argv)
{
	repo_read_index(the_repository);
	return update_working_directory(NULL);
}

static int sparse_checkout_disable(int argc, const char **argv)
{
	struct pattern_list pl;
	struct strbuf match_all = STRBUF_INIT;

	repo_read_index(the_repository);

	memset(&pl, 0, sizeof(pl));
	hashmap_init(&pl.recursive_hashmap, pl_hashmap_cmp, NULL, 0);
	hashmap_init(&pl.parent_hashmap, pl_hashmap_cmp, NULL, 0);
	pl.use_cone_patterns = 0;
	core_apply_sparse_checkout = 1;

	strbuf_addstr(&match_all, "/*");
	add_pattern(strbuf_detach(&match_all, NULL), empty_base, 0, &pl, 0);

	if (update_working_directory(&pl))
		die(_("error while refreshing working directory"));

	clear_pattern_list(&pl);
	return set_config(MODE_NO_PATTERNS);
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
			return sparse_checkout_set(argc, argv, prefix, REPLACE);
		if (!strcmp(argv[0], "add"))
			return sparse_checkout_set(argc, argv, prefix, ADD);
		if (!strcmp(argv[0], "reapply"))
			return sparse_checkout_reapply(argc, argv);
		if (!strcmp(argv[0], "disable"))
			return sparse_checkout_disable(argc, argv);
	}

	usage_with_options(builtin_sparse_checkout_usage,
			   builtin_sparse_checkout_options);
}
