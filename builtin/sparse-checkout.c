#include "builtin.h"
#include "config.h"
#include "dir.h"
#include "parse-options.h"
#include "pathspec.h"
#include "repository.h"
#include "run-command.h"
#include "sparse-checkout.h"
#include "strbuf.h"
#include "string-list.h"

static char const * const builtin_sparse_checkout_usage[] = {
	N_("git sparse-checkout [init|add|list]"),
	NULL
};

struct opts_sparse_checkout {
	const char *subcommand;
	int read_stdin;
} opts;

static char *get_sparse_checkout_filename(void)
{
	return git_pathdup("info/sparse-checkout");
}

static int check_clean_status(void)
{
/*
	struct strbuf sb = STRBUF_INIT;

	if (repo_index_has_changes(the_repository, NULL, &sb)) {
		error(_("You have local changes that could be overwritten by a reset:\n %s"),
		      sb.buf);
		return 1;
	}
*/
	return 0;
}

static int sc_read_tree(void)
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

static int sc_enable_config(void)
{
	struct argv_array argv = ARGV_ARRAY_INIT;
	int result = 0;
	argv_array_pushl(&argv, "config", "--add", "core.sparseCheckout", "true", NULL);

	if (run_command_v_opt(argv.argv, RUN_GIT_CMD)) {
		error(_("failed to enable core.sparseCheckout"));
		result = 1;
	}

	argv_array_clear(&argv);
	return result;
}

static int delete_directory(const struct object_id *oid, struct strbuf *base,
		const char *pathname, unsigned mode, int stage, void *context)
{
	struct strbuf dirname = STRBUF_INIT;
	struct stat sb;

	strbuf_addstr(&dirname, the_repository->worktree);
	strbuf_addch(&dirname, '/');
	strbuf_addstr(&dirname, pathname);

	if (stat(dirname.buf, &sb) || !(sb.st_mode & S_IFDIR))
		return 0;

	if (remove_dir_recursively(&dirname, 0))
		warning(_("failed to remove directory '%s'"),
			dirname.buf);

	strbuf_release(&dirname);
	return 0;
}

static int sparse_checkout_init(int argc, const char **argv)
{
	struct tree *t;
	struct object_id oid;
	static struct pathspec pathspec;
	struct exclude_list el;
	char *sparse_filename;
	FILE *fp;

	if (check_clean_status())
		return 1;

	if (sc_enable_config())
		return 1;

	sparse_filename = get_sparse_checkout_filename();

	if (!get_sparse_checkout_data(sparse_filename, &el)) {
		/* Check for existing data */
		goto reset;
	}

	/* initial mode: all blobs at root */
	fp = fopen(sparse_filename, "w");
	fprintf(fp, "/*\n!/*/*\n");
	fclose(fp);

	/* remove all directories in the root, if tracked by Git */
	if (get_oid("HEAD", &oid)) {
		/* assume we are in a fresh repo */
		fprintf(stderr, "NO HEAD FOUND!\n");
		free(sparse_filename);
		return 0;
	}

	t = parse_tree_indirect(&oid);

	parse_pathspec(&pathspec, PATHSPEC_ALL_MAGIC &
				  ~(PATHSPEC_FROMTOP | PATHSPEC_LITERAL),
		       PATHSPEC_PREFER_CWD,
		       "", NULL);

	if (read_tree_recursive(the_repository, t, "", 0, 0, &pathspec,
				delete_directory, NULL))
		return 1;

reset:
	free(sparse_filename);
	return sc_read_tree();
}

static int sparse_checkout_add(int argc, const char **argv)
{
	struct strbuf line = STRBUF_INIT;
	FILE *fp;
	struct exclude_list el;
	char *sparse_filename;
	struct hashmap_iter iter;
	struct exclude_entry *entry;
	struct string_list sl = STRING_LIST_INIT_DUP;
	int i;

	if (check_clean_status())
		return 1;

	memset(&el, 0, sizeof(el));
	sparse_filename = get_sparse_checkout_filename();
	get_sparse_checkout_data(sparse_filename, &el);

	if (!excludes_are_strict(&el))
		die(_("The sparse-checkout file has incompatible patterns. It may have been edited manually."));

	strbuf_init(&line, PATH_MAX);

	for (;;) {
		if (strbuf_getline(&line, stdin)) {
			if (feof(stdin))
				break;
			if (!ferror(stdin))
				die("BUG: fgets returned NULL, not EOF, not error!");
			if (errno != EINTR)
				die_errno("fgets");
			clearerr(stdin);
			continue;
		}

		if (line.len)
			insert_recursive_pattern(&el, &line);
	}

	fp = fopen(sparse_filename, "w");

	hashmap_iter_init(&el.parent_hashmap, &iter);
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

		if (!strcmp(pattern, ""))
			fprintf(fp, "/*\n!/*/*\n");
		else
			fprintf(fp, "/%s/*\n!/%s/*/*\n", pattern, pattern);
	}

	string_list_clear(&sl, 0);

	hashmap_iter_init(&el.recursive_hashmap, &iter);
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

	fclose(fp);

	return sc_read_tree();
}

static int sparse_checkout_list(int argc, const char **argv)
{
	struct exclude_list el;
	char *sparse_filename;
	int i;

	memset(&el, 0, sizeof(el));

	sparse_filename = get_sparse_checkout_filename();
	get_sparse_checkout_data(sparse_filename, &el);
	free(sparse_filename);

	if (!el.use_restricted_patterns)
		die(_("your sparse-checkout file does not use restricted patterns"));

	for (i = 0; i < el.nr; i++) {
		struct exclude *x = el.excludes[i];
		char *truncate;

		if (x->flags & EXC_FLAG_NEGATIVE)
			continue;

		if (x->patternlen < 2)
			die(_("your sparse-checkout file contains an empty pattern"));

		truncate = xstrdup(x->pattern);
		truncate[x->patternlen - 1] = 0;
		printf("%s", truncate);

		if (is_recursive_pattern(&el, truncate))
			printf("*");
		printf("\n");
		free(truncate);
	}

	return 0;
}

int cmd_sparse_checkout(int argc, const char **argv, const char *prefix)
{
	static struct option builtin_sparse_checkout_options[] = {
		OPT_END(),
	};

	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage_with_options(builtin_sparse_checkout_usage,
				   builtin_sparse_checkout_options);

	git_config(git_default_config, NULL);
	argc = parse_options(argc, argv, prefix,
			     builtin_sparse_checkout_options,
			     builtin_sparse_checkout_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);

	if (argc > 0) {
		if (!strcmp(argv[0], "init"))
			return sparse_checkout_init(argc, argv);
		if (!strcmp(argv[0], "add"))
			return sparse_checkout_add(argc, argv);
		if (!strcmp(argv[0], "list"))
			return sparse_checkout_list(argc, argv);
	}

	usage_with_options(builtin_sparse_checkout_usage,
			   builtin_sparse_checkout_options);
}
