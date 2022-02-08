#include "builtin.h"
#include "strvec.h"
#include "parse-options.h"
#include "cache.h"
#include "bundle.h"
#include "run-command.h"
#include "hashmap.h"
#include "object-store.h"
#include "refs.h"
#include "config.h"
#include "packfile.h"
#include "list-objects-filter-options.h"
#include "remote.h"

/*
 * Basic handler for bundle files to connect repositories via sneakernet.
 * Invocation must include action.
 * This function can create a bundle or provide information on an existing
 * bundle supporting "fetch", "pull", and "ls-remote".
 */

static const char * const builtin_bundle_usage[] = {
  N_("git bundle create [<options>] <file> <git-rev-list args>"),
  N_("git bundle fetch [<options>] <uri>"),
  N_("git bundle list-heads <file> [<refname>...]"),
  N_("git bundle unbundle <file> [<refname>...]"),
  N_("git bundle verify [<options>] <file>"),
  NULL
};

static const char * const builtin_bundle_create_usage[] = {
  N_("git bundle create [<options>] <file> <git-rev-list args>"),
  NULL
};

static const char * const builtin_bundle_fetch_usage[] = {
  N_("git bundle fetch [--filter=<spec>] <uri>"),
  NULL
};

static const char * const builtin_bundle_list_heads_usage[] = {
  N_("git bundle list-heads <file> [<refname>...]"),
  NULL
};

static const char * const builtin_bundle_unbundle_usage[] = {
  N_("git bundle unbundle <file> [<refname>...]"),
  NULL
};

static const char * const builtin_bundle_verify_usage[] = {
  N_("git bundle verify [<options>] <file>"),
  NULL
};

static int parse_options_cmd_bundle(int argc,
		const char **argv,
		const char* prefix,
		const char * const usagestr[],
		const struct option options[],
		char **bundle_file) {
	int newargc;
	newargc = parse_options(argc, argv, NULL, options, usagestr,
			     PARSE_OPT_STOP_AT_NON_OPTION);
	if (argc < 1)
		usage_with_options(usagestr, options);
	*bundle_file = prefix_filename(prefix, argv[0]);
	return newargc;
}

static int cmd_bundle_create(int argc, const char **argv, const char *prefix) {
	int all_progress_implied = 0;
	int progress = isatty(STDERR_FILENO);
	struct strvec pack_opts;
	int version = -1;
	int ret;
	struct option options[] = {
		OPT_SET_INT('q', "quiet", &progress,
			    N_("do not show progress meter"), 0),
		OPT_SET_INT(0, "progress", &progress,
			    N_("show progress meter"), 1),
		OPT_SET_INT(0, "all-progress", &progress,
			    N_("show progress meter during object writing phase"), 2),
		OPT_BOOL(0, "all-progress-implied",
			 &all_progress_implied,
			 N_("similar to --all-progress when progress meter is shown")),
		OPT_INTEGER(0, "version", &version,
			    N_("specify bundle format version")),
		OPT_END()
	};
	char *bundle_file;

	argc = parse_options_cmd_bundle(argc, argv, prefix,
			builtin_bundle_create_usage, options, &bundle_file);
	/* bundle internals use argv[1] as further parameters */

	strvec_init(&pack_opts);
	if (progress == 0)
		strvec_push(&pack_opts, "--quiet");
	else if (progress == 1)
		strvec_push(&pack_opts, "--progress");
	else if (progress == 2)
		strvec_push(&pack_opts, "--all-progress");
	if (progress && all_progress_implied)
		strvec_push(&pack_opts, "--all-progress-implied");

	if (!startup_info->have_repository)
		die(_("Need a repository to create a bundle."));
	ret = !!create_bundle(the_repository, bundle_file, argc, argv, &pack_opts, version);
	free(bundle_file);
	return ret;
}

static int cmd_bundle_verify(int argc, const char **argv, const char *prefix) {
	struct bundle_header header = BUNDLE_HEADER_INIT;
	int bundle_fd = -1;
	int quiet = 0;
	int ret;
	struct option options[] = {
		OPT_BOOL('q', "quiet", &quiet,
			    N_("do not show bundle details")),
		OPT_END()
	};
	char *bundle_file;

	argc = parse_options_cmd_bundle(argc, argv, prefix,
			builtin_bundle_verify_usage, options, &bundle_file);
	/* bundle internals use argv[1] as further parameters */

	if ((bundle_fd = read_bundle_header(bundle_file, &header)) < 0) {
		ret = 1;
		goto cleanup;
	}
	close(bundle_fd);
	if (verify_bundle(the_repository, &header, !quiet)) {
		ret = 1;
		goto cleanup;
	}

	fprintf(stderr, _("%s is okay\n"), bundle_file);
	ret = 0;
cleanup:
	free(bundle_file);
	bundle_header_release(&header);
	return ret;
}

/**
 * The remote_bundle_info struct contains the necessary data for
 * the list of bundles advertised by a table of contents. If the
 * bundle URI instead contains a single bundle, then this struct
 * can represent a single bundle without a 'uri' but with a
 * tempfile storing its current location on disk.
 */
struct remote_bundle_info {
	struct hashmap_entry ent;

	/**
	 * The 'id' is a name given to the bundle for reference
	 * by other bundle infos.
	 */
	char *id;

	/**
	 * The 'uri' is the location of the remote bundle so
	 * it can be downloaded on-demand. This will be NULL
	 * if there was no table of contents.
	 */
	char *uri;

	/**
	 * The 'requires_id' string, if non-NULL, contains the 'id'
	 * for a bundle that contains the prerequisites for this
	 * bundle. Used by table of contents to allow fetching
	 * a portion of a repository incrementally.
	 */
	char *requires_id;

	/**
	 * The 'filter_str' string, if non-NULL, specifies the
	 * filter capability exists in this bundle with the given
	 * specification. Allows selecting bundles that match the
	 * client's desired filter. If NULL, then no filter exists
	 * on the bundle.
	 */
	char *filter_str;

	/**
	 * A table of contents can include a timestamp for the
	 * bundle as a heuristic for describing a list of bundles
	 * in order of recency.
	 */
	timestamp_t timestamp;

	/**
	 * If the bundle has been downloaded, then 'file' is a
	 * filename storing its contents. Otherwise, 'file' is
	 * an empty string.
	 */
	struct strbuf file;

	/**
	 * The 'stack_next' pointer allows this struct to form
	 * a stack.
	 */
	struct remote_bundle_info *stack_next;

	/**
	 * 'pushed' is set when first pushing the required bundle
	 * onto the stack. Used to error out when verifying the
	 * prerequisites and avoiding an infinite loop.
	 */
	unsigned pushed:1;
};

static int remote_bundle_cmp(const void *unused_cmp_data,
			     const struct hashmap_entry *a,
			     const struct hashmap_entry *b,
			     const void *key)
{
	const struct remote_bundle_info *ee1 =
			container_of(a, struct remote_bundle_info, ent);
	const struct remote_bundle_info *ee2 =
			container_of(b, struct remote_bundle_info, ent);

	return strcmp(ee1->id, ee2->id);
}

static int parse_toc_config(const char *key, const char *value, void *data)
{
	struct hashmap *toc = data;
	const char *key1, *key2, *id_end;
	struct strbuf id = STRBUF_INIT;
	struct remote_bundle_info info_lookup;
	struct remote_bundle_info *info;

	if (!skip_prefix(key, "bundle.", &key1))
		return -1;

	if (skip_prefix(key1, "tableofcontents.", &key2)) {
		if (!strcmp(key2, "version")) {
			int version = git_config_int(key, value);

			if (version != 1) {
				warning(_("table of contents version %d not understood"), version);
				return -1;
			}
		}

		return 0;
	}

	id_end = strchr(key1, '.');

	/*
	 * If this key is of the form "bundle.<x>" with no third item,
	 * then we do not know about it. We should ignore it. Later versions
	 * might start caring about this data on an optional basis. Increase
	 * the version number to add keys that must be understood.
	 */
	if (!id_end)
		return 0;

	strbuf_add(&id, key1, id_end - key1);
	key2 = id_end + 1;

	info_lookup.id = id.buf;
	hashmap_entry_init(&info_lookup.ent, strhash(info_lookup.id));
	if (!(info = hashmap_get_entry(toc, &info_lookup, ent, NULL))) {
		CALLOC_ARRAY(info, 1);
		info->id = strbuf_detach(&id, NULL);
		strbuf_init(&info->file, 0);
		hashmap_entry_init(&info->ent, strhash(info->id));
		hashmap_add(toc, &info->ent);
	}

	if (!strcmp(key2, "uri")) {
		if (info->uri)
			warning(_("duplicate 'uri' value for id '%s'"), info->id);
		else
			info->uri = xstrdup(value);
		return 0;
	} else if (!strcmp(key2, "timestamp")) {
		if (info->timestamp)
			warning(_("duplicate 'timestamp' value for id '%s'"), info->id);
		else
			info->timestamp = git_config_int64(key, value);
		return 0;
	} else if (!strcmp(key2, "requires")) {
		if (info->requires_id)
			warning(_("duplicate 'requires' value for id '%s'"), info->id);
		else
			info->requires_id = xstrdup(value);
		return 0;
	} else if (!strcmp(key2, "filter")) {
		if (info->filter_str)
			warning(_("duplicate 'filter' value for id '%s'"), info->id);
		else
			info->filter_str = xstrdup(value);
		return 0;
	}

	/* Return 0 here to ignore unknown options. */
	return 0;
}

static void download_uri_to_file(const char *uri, const char *file)
{
	struct child_process cp = CHILD_PROCESS_INIT;
	FILE *child_in;

	strvec_pushl(&cp.args, "git-remote-https", "origin", uri, NULL);
	cp.in = -1;
	cp.out = -1;

	if (start_command(&cp))
		die(_("failed to start remote helper"));

	child_in = fdopen(cp.in, "w");
	if (!child_in)
		die(_("cannot write to child process"));

	fprintf(child_in, "get %s %s\n\n", uri, file);
	fclose(child_in);

	if (finish_command(&cp))
		die(_("remote helper failed"));
}

static void find_temp_filename(struct strbuf *name)
{
	int fd;
	/*
	 * Find a temporray filename that is available. This is briefly
	 * racy, but unlikely to collide.
	 */
	fd = odb_mkstemp(name, "bundles/tmp_uri_XXXXXX");
	if (fd < 0)
		die(_("failed to create temporary file"));
	close(fd);
	unlink(name->buf);
}

static void unbundle_fetched_bundle(struct remote_bundle_info *info)
{
	struct child_process cp = CHILD_PROCESS_INIT;
	FILE *f;
	struct strbuf line = STRBUF_INIT;
	struct strbuf bundle_ref = STRBUF_INIT;
	size_t bundle_prefix_len;

	strvec_pushl(&cp.args, "bundle", "unbundle",
				info->file.buf, NULL);
	cp.git_cmd = 1;
	cp.out = -1;

	if (start_command(&cp))
		die(_("failed to start 'unbundle' process"));

	strbuf_addstr(&bundle_ref, "refs/bundles/");
	bundle_prefix_len = bundle_ref.len;

	f = fdopen(cp.out, "r");
	while (strbuf_getline(&line, f) != EOF) {
		struct object_id oid, old_oid;
		const char *refname, *branch_name, *end;
		char *space;
		int has_old;

		strbuf_trim_trailing_newline(&line);

		space = strchr(line.buf, ' ');

		if (!space)
			continue;

		refname = space + 1;
		*space = '\0';
		parse_oid_hex(line.buf, &oid, &end);

		/* TODO: write refs into refs/bundles */
		if (!skip_prefix(refname, "refs/heads/", &branch_name))
			continue;

		strbuf_setlen(&bundle_ref, bundle_prefix_len);
		strbuf_addstr(&bundle_ref, branch_name);

		/* TODO: consider replacing with a ref transaction */
		has_old = !read_ref(bundle_ref.buf, &old_oid);

		update_ref("bundle fetch", bundle_ref.buf, &oid,
				has_old ? &old_oid : NULL,
				REF_SKIP_OID_VERIFICATION,
				UPDATE_REFS_MSG_ON_ERR);
	}

	if (finish_command(&cp))
		die(_("failed to unbundle bundle from '%s'"), info->uri);

	unlink_or_warn(info->file.buf);
}

static int cmd_bundle_fetch(int argc, const char **argv, const char *prefix)
{
	int ret = 0, used_hashmap = 0;
	int progress = isatty(2);
	char *bundle_uri;
	struct remote_bundle_info first_file = {
		.file = STRBUF_INIT,
	};
	struct remote_bundle_info *stack = NULL;
	struct hashmap toc = { 0 };
	const char *filter = NULL;
	const char *timestamp_key = "fetch.bundletimestamp";
	timestamp_t stored_time = 0;
	timestamp_t max_time = 0;

	struct option options[] = {
		OPT_BOOL(0, "progress", &progress,
			 N_("show progress meter")),
		OPT_STRING(0, "filter", &filter,
			   N_("filter-spec"), N_("only install bundles matching this filter")),
		OPT_END()
	};

	argc = parse_options_cmd_bundle(argc, argv, prefix,
			builtin_bundle_fetch_usage, options, &bundle_uri);

	if (!startup_info->have_repository)
		die(_("'fetch' requires a repository"));

	git_config_get_timestamp(timestamp_key, &stored_time);

	/*
	 * Step 1: determine protocol for uri, and download contents to
	 * a temporary location.
	 */
	first_file.uri = bundle_uri;
	find_temp_filename(&first_file.file);
	download_uri_to_file(bundle_uri, first_file.file.buf);

	/*
	 * Step 2: Check if the file is a bundle (if so, add it to the
	 * stack and move to step 3). If it is a table of contents,
	 * determine which of the bundles are appropriate to download and
	 * add them to the stack.
	 */

	if (is_bundle(first_file.file.buf, 1)) {
		/* The simple case: only one file, no stack to worry about. */
		stack = &first_file;
	} else {
		struct hashmap_iter iter;
		struct remote_bundle_info *info;

		/* populate a hashtable with all relevant bundles. */
		used_hashmap = 1;
		hashmap_init(&toc, remote_bundle_cmp, NULL, 0);
		git_config_from_file(parse_toc_config, first_file.file.buf, &toc);

		/* initialize stack using timestamp heuristic. */
		hashmap_for_each_entry(&toc, &iter, info, ent) {
			char *old_uri;

			/* Skip if filter does not match. */
			if (!filter && info->filter_str)
				continue;
			if (filter &&
			    (!info->filter_str || strcasecmp(filter, info->filter_str)))
				continue;

			old_uri = info->uri;
			info->uri = relative_url(bundle_uri, info->uri, NULL);
			free(old_uri);

			/*
			 * Now that the filter matches, start with the
			 * bundle with largest timestamp.
			 */
			if (info->timestamp > max_time || !stack) {
				stack = info;
				max_time = info->timestamp;
			}
		}

		trace2_data_intmax("bundle", the_repository, "max_time", max_time);
		trace2_data_intmax("bundle", the_repository, "stored_time", stored_time);

		/* Skip fetching bundles if data isn't new enough. */
		if (max_time <= stored_time)
			goto cleanup;
	}

	/*
	 * Step 3: For each bundle in the stack:
	 * 	i. If not downloaded to a temporary file, download it.
	 * 	ii. Once downloaded, check that its prerequisites are in
	 * 	    the object database. If not, then push its dependent
	 * 	    bundle onto the stack. (Fail if no such bundle exists.)
	 * 	iii. If all prerequisites are present, then unbundle the
	 * 	     temporary file and pop the bundle from the stack.
	 */
	while (stack) {
		int valid = 1;
		int bundle_fd;
		const char *filter_str = NULL;
		struct string_list_item *prereq;
		struct bundle_header header = BUNDLE_HEADER_INIT;

		if (!stack->file.len) {
			find_temp_filename(&stack->file);
			download_uri_to_file(stack->uri, stack->file.buf);
			if (!is_bundle(stack->file.buf, 1))
				die(_("file downloaded from '%s' is not a bundle"), stack->uri);
		}

		/*
		 * TODO: check prerequisites and possibly push
		 * onto the stack from the hashtable.
		 */
		bundle_header_init(&header);
		bundle_fd = read_bundle_header(stack->file.buf, &header);
		if (bundle_fd < 0)
			die(_("failed to read bundle from '%s'"), stack->uri);

		if (header.filter)
			filter_str = list_objects_filter_spec(header.filter);

		if (filter && (!filter_str || strcasecmp(filter, filter_str)))
			die(_("bundle from '%s' does not match expected filter"),
			    stack->uri);
		if (!filter && filter_str)
			die(_("bundle from '%s' has an unexpected filter"),
			    stack->uri);

		reprepare_packed_git(the_repository);
		for_each_string_list_item(prereq, &header.prerequisites) {
			struct object_info info = OBJECT_INFO_INIT;
			struct object_id *oid = prereq->util;

			if (oid_object_info_extended(the_repository, oid, &info,
						     OBJECT_INFO_QUICK)) {
				valid = 0;
				break;
			}
		}

		close(bundle_fd);
		bundle_header_release(&header);

		if (valid) {
			unbundle_fetched_bundle(stack);
		} else if (stack->pushed) {
			die(_("bundle '%s' still invalid after downloading required bundle"), stack->id);
		} else if (stack->requires_id) {
			/*
			 * Load the next bundle from the hashtable and
			 * push it onto the stack.
			 */
			struct remote_bundle_info *info;
			struct remote_bundle_info info_lookup = { 0 };
			info_lookup.id = stack->requires_id;

			hashmap_entry_init(&info_lookup.ent, strhash(info_lookup.id));
			if ((info = hashmap_get_entry(&toc, &info_lookup, ent, NULL))) {
				/* Push onto the stack */
				stack->pushed = 1;
				info->stack_next = stack;
				stack = info;
				continue;
			} else {
				die(_("unable to find bundle '%s' required by bundle '%s'"),
				    stack->requires_id, stack->id);
			}
		} else {
			die(_("bundle from '%s' has missing prerequisites and no dependent bundle"),
			    stack->uri);
		}

		stack = stack->stack_next;
	}

	if (max_time) {
		struct strbuf tstr = STRBUF_INIT;
		strbuf_addf(&tstr, "%"PRIuMAX"", max_time);
		git_config_set_gently(timestamp_key, tstr.buf);
		strbuf_release(&tstr);
	}

cleanup:
	if (used_hashmap) {
		struct hashmap_iter iter;
		struct remote_bundle_info *info;
		hashmap_for_each_entry(&toc, &iter, info, ent) {
			free(info->id);
			free(info->uri);
			free(info->requires_id);
			free(info->filter_str);
		}
		hashmap_clear_and_free(&toc, struct remote_bundle_info, ent);
	}
	free(bundle_uri);
	return ret;
}

static int cmd_bundle_list_heads(int argc, const char **argv, const char *prefix) {
	struct bundle_header header = BUNDLE_HEADER_INIT;
	int bundle_fd = -1;
	int ret;
	struct option options[] = {
		OPT_END()
	};
	char *bundle_file;

	argc = parse_options_cmd_bundle(argc, argv, prefix,
			builtin_bundle_list_heads_usage, options, &bundle_file);
	/* bundle internals use argv[1] as further parameters */

	if ((bundle_fd = read_bundle_header(bundle_file, &header)) < 0) {
		ret = 1;
		goto cleanup;
	}
	close(bundle_fd);
	ret = !!list_bundle_refs(&header, argc, argv);
cleanup:
	free(bundle_file);
	bundle_header_release(&header);
	return ret;
}

static int cmd_bundle_unbundle(int argc, const char **argv, const char *prefix) {
	struct bundle_header header = BUNDLE_HEADER_INIT;
	int bundle_fd = -1;
	int ret;
	int progress = isatty(2);

	struct option options[] = {
		OPT_BOOL(0, "progress", &progress,
			 N_("show progress meter")),
		OPT_END()
	};
	char *bundle_file;
	struct strvec extra_index_pack_args = STRVEC_INIT;

	argc = parse_options_cmd_bundle(argc, argv, prefix,
			builtin_bundle_unbundle_usage, options, &bundle_file);
	/* bundle internals use argv[1] as further parameters */

	if ((bundle_fd = read_bundle_header(bundle_file, &header)) < 0) {
		ret = 1;
		goto cleanup;
	}
	if (!startup_info->have_repository)
		die(_("Need a repository to unbundle."));
	if (progress)
		strvec_pushl(&extra_index_pack_args, "-v", "--progress-title",
			     _("Unbundling objects"), NULL);
	ret = !!unbundle(the_repository, &header, bundle_fd,
			 &extra_index_pack_args) ||
		list_bundle_refs(&header, argc, argv);
	bundle_header_release(&header);
cleanup:
	free(bundle_file);
	return ret;
}

int cmd_bundle(int argc, const char **argv, const char *prefix)
{
	struct option options[] = {
		OPT_END()
	};
	int result;

	argc = parse_options(argc, argv, prefix, options, builtin_bundle_usage,
		PARSE_OPT_STOP_AT_NON_OPTION);

	packet_trace_identity("bundle");

	if (argc < 2)
		usage_with_options(builtin_bundle_usage, options);

	else if (!strcmp(argv[0], "create"))
		result = cmd_bundle_create(argc, argv, prefix);
	else if (!strcmp(argv[0], "fetch"))
		result = cmd_bundle_fetch(argc, argv, prefix);
	else if (!strcmp(argv[0], "list-heads"))
		result = cmd_bundle_list_heads(argc, argv, prefix);
	else if (!strcmp(argv[0], "unbundle"))
		result = cmd_bundle_unbundle(argc, argv, prefix);
	else if (!strcmp(argv[0], "verify"))
		result = cmd_bundle_verify(argc, argv, prefix);
	else {
		error(_("Unknown subcommand: %s"), argv[0]);
		usage_with_options(builtin_bundle_usage, options);
	}
	return result ? 1 : 0;
}
