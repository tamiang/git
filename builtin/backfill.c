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
	fprintf(stderr, "Downloading a batch of size %"PRIuMAX"\n", batch->nr);
	promisor_remote_get_direct(the_repository, batch->oid, batch->nr);
	oid_array_clear(batch);
	return 0;
}

static int add_children(const char *base_path,
			struct object_id *oid,
			struct strmap *paths_to_store,
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

		/* Skip symlinks and commits. */
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
		}
		oid_array_append(&list->oids, &entry.oid);
	}

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
static int do_backfill_for_depth(struct strmap *paths_to_explore,
				 struct strmap *paths_to_store,
				 struct oid_array *batch,
				 struct oidset *added,
				 int depth)
{
	struct hashmap_iter iter;
	struct strmap_entry *e;

	trace2_region_enter("backfill", "do_backfill_for_depth", the_repository);
	hashmap_for_each_entry(&paths_to_explore->map, &iter, e, ent) {
		struct type_and_oid_list *list = e->value;

		if (list->type == OBJ_TREE) {
			for (size_t i = 0; i < list->oids.nr; i++) {
				add_children((const char *)e->key,
					     &list->oids.oid[i],
					     paths_to_store,
					     batch,
					     added);
			}
		} else {
			fill_missing_blobs((const char *)e->key,
					   &list->oids,
					   batch);
		}

		oid_array_clear(&list->oids);
	}

	trace2_region_leave("backfill", "do_backfill_for_depth", the_repository);
	return hashmap_get_size(&paths_to_store->map);
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
		commits_nr++;
		oid_array_append(&list->oids, get_commit_tree_oid(c));
	}

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
	struct strmap base = STRMAP_INIT;
	struct strmap next = STRMAP_INIT;
	int ret;
	struct strmap *cur_base = &base;
	struct strmap *cur_next = &next;
	struct oid_array batch = OID_ARRAY_INIT;
	struct oidset added = OIDSET_INIT;
	int depth = 0;

	if (initialize_backfill(&base))
		die(_("failed to backfil during commit walk"));

	/* */
	while ((ret = do_backfill_for_depth(cur_base,
					    cur_next,
					    &batch,
					    &added,
					    depth++)) > 0) {
		struct strmap *t;

		clear_backfill_strmap(cur_base);
		t = cur_base;
		cur_base = cur_next;
		cur_next = t;
	}

	if (ret)
		die(_("failed to walk trees"));

	ret = download_batch(&batch);
	clear_backfill_strmap(&base);
	clear_backfill_strmap(&next);
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

	return do_backfill();
}
