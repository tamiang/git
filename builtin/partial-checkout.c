#include "builtin.h"
#include "config.h"
#include "dir.h"
#include "parse-options.h"
#include "partial-checkout.h"
#include "pathspec.h"
#include "repository.h"
#include "run-command.h"
#include "strbuf.h"
#include "string-list.h"

static char const * const builtin_partial_checkout_usage[] = {
	N_("git partial-checkout [init|add|remove|list]"),
	NULL
};

struct opts_partial_checkout {
	const char *subcommand;
	int read_stdin;
} opts;

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

static int pc_read_tree(void)
{
	struct argv_array argv = ARGV_ARRAY_INIT;
	int result = 0;
	argv_array_pushl(&argv, "read-tree", "-m", "-u", "HEAD", NULL);

	if (run_command_v_opt(argv.argv, RUN_GIT_CMD)) {
		error(_("failed to update index with new partial-checkout paths"));
		result = 1;
	}

	argv_array_clear(&argv);
	return result;
}

static int pc_reset_hard(void)
{
	struct argv_array argv = ARGV_ARRAY_INIT;
	int result = 0;
	argv_array_pushl(&argv, "reset", "--hard", "HEAD", NULL);

	if (run_command_v_opt(argv.argv, RUN_GIT_CMD)) {
		error(_("failed to reset with new partial-checkout paths"));
		result = 1;
	}

	argv_array_clear(&argv);
	return result;
}

static int pc_enable_config(void)
{
	struct argv_array argv = ARGV_ARRAY_INIT;
	int result = 0;
	argv_array_pushl(&argv, "config", "core.partialCheckout", "true", NULL);

	if (run_command_v_opt(argv.argv, RUN_GIT_CMD)) {
		error(_("failed to enable core.partialCheckout"));
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

static int partial_checkout_init(int argc, const char **argv)
{
	struct tree *t;
	struct object_id oid;
	static struct pathspec pathspec;

	if (check_clean_status())
		return 1;

	if (pc_enable_config())
		return 1;

	/* remove all directories in the root, if tracked by Git */
	if (get_oid("HEAD", &oid))
		die(_("unable to parse HEAD"));
	t = parse_tree_indirect(&oid);

	parse_pathspec(&pathspec, PATHSPEC_ALL_MAGIC &
				  ~(PATHSPEC_FROMTOP | PATHSPEC_LITERAL),
		       PATHSPEC_PREFER_CWD,
		       "", NULL);

	if (read_tree_recursive(the_repository, t, "", 0, 0, &pathspec,
				delete_directory, NULL));

	return pc_read_tree() || pc_reset_hard();
}

static int partial_checkout_add(int argc, const char **argv)
{
	struct strbuf data = STRBUF_INIT;
	char line[PATH_MAX + 2];
	char *fname;
	FILE *fp;
	int i, start = 0;

	if (check_clean_status())
		return 1;

	get_partial_checkout_data(the_repository, &data);

	for (;;) {
		if (!fgets(line, sizeof(line), stdin)) {
			if (feof(stdin))
				break;
			if (!ferror(stdin))
				die("BUG: fgets returned NULL, not EOF, not error!");
			if (errno != EINTR)
				die_errno("fgets");
			clearerr(stdin);
			continue;
		}

		if (strlen(line)) {
			strbuf_addstr(&data, line);
			strbuf_addch(&data, '\n');
		}
	}

	fname = get_partial_checkout_filename(the_repository);
	fp = fopen(fname, "w");
	free(fname);

	for (i = 0; i < data.len; i++) {
		if (data.buf[i] == '\n') {
			if (i > start + 1) {
				data.buf[i] = '\0';
				fprintf(fp, "%s\n", data.buf + start);
			}

			start = i + 1;
		}
	}

	fclose(fp);
	strbuf_release(&data);

	return pc_read_tree() || pc_reset_hard();
}

static int partial_checkout_remove(int argc, const char **argv)
{
	struct strbuf data = STRBUF_INIT;
	struct string_list to_remove = STRING_LIST_INIT_DUP;
	char line[PATH_MAX + 2];
	char *fname;
	FILE *fp;
	int i, start = 0;
	struct strbuf dirname = STRBUF_INIT;
	size_t dirname_len = dirname.len;

	if (check_clean_status())
		return 1;

	string_list_init(&to_remove, 0);
	strbuf_addstr(&dirname, the_repository->worktree);

	for (;;) {
		int len;

		if (!fgets(line, sizeof(line), stdin)) {
			if (feof(stdin))
				break;
			if (!ferror(stdin))
				die("BUG: fgets returned NULL, not EOF, not error!");
			if (errno != EINTR)
				die_errno("fgets");
			clearerr(stdin);
			continue;
		}

		if ((len = strlen(line)) > 1) {
			if (line[len - 1] == '\n')
				line[len - 1] = '\0';

			string_list_insert(&to_remove, line);

			strbuf_addf(&dirname, "/%s", line);

			if (remove_dir_recursively(&dirname, 0))
				warning(_("failed to remove directory '%s'"), dirname.buf);

			strbuf_setlen(&dirname, dirname_len);
		}
	}
	string_list_sort(&to_remove);

	get_partial_checkout_data(the_repository, &data);
	fname = get_partial_checkout_filename(the_repository);
	fp = fopen(fname, "w");
	free(fname);

	for (i = 0; i < data.len; i++) {
		if (data.buf[i] == '\n') {
			if (i > start + 1) {
				data.buf[i] = '\0';

				if (!string_list_has_string(&to_remove, data.buf + start))
					fprintf(fp, "%s\n", data.buf + start);
			}

			start = i + 1;
		}
	}

	fclose(fp);
	strbuf_release(&data);
	string_list_clear(&to_remove, 0);

	return pc_read_tree() || pc_reset_hard();
}

static int partial_checkout_list(int argc, const char **argv)
{
	struct strbuf data = STRBUF_INIT;

	get_partial_checkout_data(the_repository, &data);
	printf("%s\n", data.buf);

	return 0;
}

int cmd_partial_checkout(int argc, const char **argv, const char *prefix)
{
	static struct option builtin_partial_checkout_options[] = {
		OPT_END(),
	};

	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage_with_options(builtin_partial_checkout_usage,
				   builtin_partial_checkout_options);

	git_config(git_default_config, NULL);
	argc = parse_options(argc, argv, prefix,
			     builtin_partial_checkout_options,
			     builtin_partial_checkout_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);

	if (argc > 0) {
		if (!strcmp(argv[0], "init"))
			return partial_checkout_init(argc, argv);
		if (!strcmp(argv[0], "add"))
			return partial_checkout_add(argc, argv);
		if (!strcmp(argv[0], "remove"))
			return partial_checkout_remove(argc, argv);
		if (!strcmp(argv[0], "list"))
			return partial_checkout_list(argc, argv);
	}

	usage_with_options(builtin_partial_checkout_usage,
			   builtin_partial_checkout_options);
}
