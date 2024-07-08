#include "builtin.h"
#include "git-compat-util.h"
#include "config.h"
#include "parse-options.h"
#include "repository.h"
#include "commit.h"
#include "hex.h"
#include "tree.h"
#include "tree-walk.h"
#include "object.h"
#include "object-store-ll.h"
#include "oid-array.h"
#include "oidset.h"
#include "promisor-remote.h"
#include "strmap.h"
#include "string-list.h"
#include "revision.h"
#include "trace2.h"
#include "progress.h"
#include "packfile.h"
#include "gvfs-helper-client.h"

static const char * const builtin_backfill_usage[] = {
	N_("git backfill [<options>]"),
	NULL
};

const size_t batch_size = 16000;

struct type_and_oid_list
{
	enum object_type type;
	struct oid_array oids;
};

#define TYPE_AND_OID_LIST_INIT { \
	.type = OBJ_NONE, 	 \
	.oids = OID_ARRAY_INIT	 \
}

static int download_batch(struct oid_array *batch)
{
	promisor_remote_get_direct(the_repository, batch->oid, batch->nr);
	oid_array_clear(batch);
	reprepare_packed_git(the_repository);
	return 0;
}

static int add_children(const char *base_path,
			struct object_id *oid,
			struct strmap *paths_to_store,
			struct string_list *path_stack,
			struct oid_array *batch,
			struct oidset *added)
{

	struct tree *tree = lookup_tree(the_repository, oid);
	struct tree_desc desc;
	struct name_entry entry;
	struct strbuf path = STRBUF_INIT;
	size_t base_len;

	if (!tree) {
		error(_("missing tree? %s"), oid_to_hex(oid));
		return -1;
	}

	strbuf_addstr(&path, base_path);
	base_len = path.len;

	parse_tree(tree);
	init_tree_desc(&desc, &tree->object.oid, tree->buffer, tree->size);
	while (tree_entry(&desc, &entry)) {
		struct type_and_oid_list *list;
		/* Not actually true, but we will ignore submodules later. */
		enum object_type type = S_ISDIR(entry.mode) ? OBJ_TREE : OBJ_BLOB;

		/* Skip submodules. */
		if (S_ISGITLINK(entry.mode))
			continue;

		if (oidset_contains(added, &entry.oid))
			continue;
		oidset_insert(added, &entry.oid);

		strbuf_setlen(&path, base_len);
		strbuf_add(&path, entry.path, entry.pathlen);

		/*
		 * Trees will end with "/" for concatenation and distinction
		 * from blobs at the same path.
		 */
		if (type == OBJ_TREE)
			strbuf_addch(&path, '/');

		if (!(list = strmap_get(paths_to_store, path.buf))) {
			CALLOC_ARRAY(list, 1);
			list->type = type;
			strmap_put(paths_to_store, path.buf, list);
			string_list_append(path_stack, path.buf);
		}
		oid_array_append(&list->oids, &entry.oid);
	}

	free_tree_buffer(tree);
	strbuf_release(&path);
	return 0;
}

static int fill_missing_blobs(const char *path,
			      struct oid_array *list,
			      struct oid_array *batch)
{
	for (size_t i = 0; i < list->nr; i++) {
		struct object_info info = OBJECT_INFO_INIT;
		if (oid_object_info_extended(the_repository,
					     &list->oid[i],
					     &info,
					     OBJECT_INFO_FOR_PREFETCH))
			oid_array_append(batch, &list->oid[i]);
	}

	if (batch->nr >= batch_size)
		download_batch(batch);

	return 0;
}

/*
 * For each path in paths_to_explore, walk the trees another level
 * and add any found blobs to the batch (but only if they don't
 * exist and haven't been added yet).
 */
static int do_backfill_for_path(const char *path,
	 			struct strmap *paths_to_lists,
				struct string_list *path_stack,
				struct oid_array *batch,
				struct oidset *added)
{
	struct type_and_oid_list *list = strmap_get(paths_to_lists, path);

	if (list->type == OBJ_TREE) {
		for (size_t i = 0; i < list->oids.nr; i++) {
			add_children(path,
				     &list->oids.oid[i],
				     paths_to_lists,
				     path_stack,
				     batch,
				     added);
		}
	} else {
		fill_missing_blobs(path,
				   &list->oids,
				   batch);
	}

	oid_array_clear(&list->oids);
	strmap_remove(paths_to_lists, path, 1);

	return 0;
}

/**
 * Walk all commits, filling the root path (empty string) with
 * all root tree object IDs.
 *
 * TODO: specify some refs other than HEAD
 */
static int initialize_backfill(struct strmap *paths_to_store)
{
	struct rev_info revs;
	struct commit *c;
	struct type_and_oid_list *list;
	size_t commits_nr = 0;
	struct progress *progress = start_progress("Exploring commit history", 0);

	repo_init_revisions(the_repository, &revs, "");
	handle_revision_arg("HEAD", &revs, 0, 0);
	handle_revision_arg("--topo-order", &revs, 0, 0);

	if (prepare_revision_walk(&revs))
		die(_("failed to setup revision walk"));

	trace2_region_enter("backfill", "initialize_backfill", the_repository);

	/* Insert a single list for the root tree into the paths. */
	CALLOC_ARRAY(list, 1);
	list->type = OBJ_TREE;
	strmap_put(paths_to_store, "", list);

	while ((c = get_revision(&revs)))
	{
		display_progress(progress, ++commits_nr);
		oid_array_append(&list->oids, get_commit_tree_oid(c));
	}

	stop_progress(&progress);
	release_revisions(&revs);
	trace2_data_intmax("backfill", the_repository, "commits", commits_nr);
	trace2_region_leave("backfill", "initialize_backfill", the_repository);

	return 0;
}

static void clear_backfill_strmap(struct strmap *map)
{
	struct hashmap_iter iter;
	struct strmap_entry *e;

	hashmap_for_each_entry(&map->map, &iter, e, ent) {
		struct type_and_oid_list *list = e->value;
		oid_array_clear(&list->oids);
	}
	strmap_clear(map, 1);
	strmap_init(map);
}

static int do_backfill(void)
{
	/*
	 * Store data by depth, then by path.
	 */
	struct strmap paths_to_list = STRMAP_INIT;
	int ret = 0;
	struct oid_array batch = OID_ARRAY_INIT;
	struct oidset added = OIDSET_INIT;
	struct string_list stack = STRING_LIST_INIT_DUP;
	size_t paths_nr = 0;
	struct progress *progress;

	if (initialize_backfill(&paths_to_list))
		die(_("failed to backfill during commit walk"));

	string_list_append(&stack, "");

	progress = start_progress("Exploring paths", 0);
	display_progress(progress, 0);

	while (!ret && stack.nr) {
		char *path = stack.items[stack.nr - 1].string;
		stack.nr--;

		ret = do_backfill_for_path(path,
					   &paths_to_list,
					   &stack,
					   &batch,
					   &added);

		display_progress(progress, ++paths_nr);
		free(path);
	}

	stop_progress(&progress);

	ret = download_batch(&batch);
	clear_backfill_strmap(&paths_to_list);
	oidset_clear(&added);
	return ret;
}

int cmd_backfill(int argc, const char **argv, const char *prefix)
{
	struct option options[] = {
		OPT_END(),
	};

	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage_with_options(builtin_backfill_usage, options);

	argc = parse_options(argc, argv, prefix, options, builtin_backfill_usage,
			     0);

	git_config(git_default_config, NULL);

	gh_client__init_block_size(batch_size * 4);

	return do_backfill();
}
