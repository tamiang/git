/*
 * path-walk.c: implementation for path-based walks of the object graph.
 */
#include "git-compat-util.h"
#include "path-walk.h"
#include "blob.h"
#include "commit.h"
#include "dir.h"
#include "hashmap.h"
#include "hex.h"
#include "object.h"
#include "oid-array.h"
#include "progress.h"
#include "revision.h"
#include "string-list.h"
#include "strmap.h"
#include "trace2.h"
#include "tree.h"
#include "tree-walk.h"

struct type_and_oid_list
{
	enum object_type type;
	struct oid_array oids;
	int has_interesting;
};

#define TYPE_AND_OID_LIST_INIT { \
	.type = OBJ_NONE, 	 \
	.oids = OID_ARRAY_INIT	 \
	.has_interesting = 0	 \
}

static int add_children(struct path_walk_info *info,
			const char *base_path,
			struct object_id *oid,
			struct strmap *paths_to_lists,
			struct string_list *path_stack,
			struct oidset *added)
{
	struct tree_desc desc;
	struct name_entry entry;
	struct strbuf path = STRBUF_INIT;
	size_t base_len;
	struct tree *tree = lookup_tree(the_repository, oid);

	if (!tree) {
		error(_("failed to walk children of tree %s: not found"),
		      oid_to_hex(oid));
		return -1;
	}

	strbuf_addstr(&path, base_path);
	base_len = path.len;

	parse_tree(tree);
	init_tree_desc(&desc, &tree->object.oid, tree->buffer, tree->size);
	while (tree_entry(&desc, &entry)) {
		struct type_and_oid_list *list;
		struct object *o;
		/* Not actually true, but we will ignore submodules later. */
		enum object_type type = S_ISDIR(entry.mode) ? OBJ_TREE : OBJ_BLOB;

		/* Skip submodules. */
		if (S_ISGITLINK(entry.mode))
			continue;

		if (oidset_contains(added, &entry.oid))
			continue;

		if (type == OBJ_TREE) {
			struct tree *child = lookup_tree(info->revs->repo, &entry.oid);
			o = child ? &child->object : NULL;
		} else if (type == OBJ_BLOB) {
			struct blob *child = lookup_blob(info->revs->repo, &entry.oid);
			o = child ? &child->object : NULL;
		}

		if (!o) /* report error?*/
			continue;

		/*
		 * Pass uninteresting flag, if necessary. This must be done
		 * before checking the SEEN flag, in case this object was added
		 * from an interesting object first.
		 */
		if (tree->object.flags & UNINTERESTING)
			o->flags |= UNINTERESTING;

		/* Skip this object if already seen. */
		if (o->flags & SEEN)
			continue;
		o->flags |= SEEN;
		oidset_insert(added, &entry.oid);

		strbuf_setlen(&path, base_len);
		strbuf_add(&path, entry.path, entry.pathlen);

		/*
		 * Trees will end with "/" for concatenation and distinction
		 * from blobs at the same path.
		 */
		if (type == OBJ_TREE)
			strbuf_addch(&path, '/');

		if (!(list = strmap_get(paths_to_lists, path.buf))) {
			CALLOC_ARRAY(list, 1);
			list->type = type;
			strmap_put(paths_to_lists, path.buf, list);
			string_list_append(path_stack, path.buf);
		}
		oid_array_append(&list->oids, &entry.oid);
	}

	free_tree_buffer(tree);
	strbuf_release(&path);
	return 0;
}

/*
 * For each path in paths_to_explore, walk the trees another level
 * and add any found blobs to the batch (but only if they don't
 * exist and haven't been added yet).
 */
static int walk_path(struct path_walk_info *info,
		     const char *path,
	 	     struct strmap *paths_to_lists,
		     struct string_list *path_stack,
		     struct oidset *added)
{
	struct type_and_oid_list *list = strmap_get(paths_to_lists, path);
	int ret = 0;

	if (info->skip_all_uninteresting) {
		int found_interesting = 0;
		for (size_t i = 0; i < list->oids.nr; i++) {
			struct object *o = lookup_object(info->revs->repo,
						         &list->oids.oid[i]);

			if (!(o->flags & UNINTERESTING)) {
				found_interesting = 1;
				break;
			}
		}

		if (!found_interesting)
			goto cleanup;
	}

	/* Evaluate function pointer on this data. */
	ret = info->path_fn(path, &list->oids, list->type, info->path_fn_data);

	/* Expand data for children. */
	if (list->type == OBJ_TREE) {
		for (size_t i = 0; i < list->oids.nr; i++) {
			ret |= add_children(info,
					    path,
					    &list->oids.oid[i],
					    paths_to_lists,
					    path_stack,
					    added);
		}
	}

cleanup:
	oid_array_clear(&list->oids);
	strmap_remove(paths_to_lists, path, 1);

	return ret;
}

static void clear_strmap(struct strmap *map)
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

/**
 * Given the configuration of 'info', walk the commits based on 'info->revs' and
 * call 'info->path_fn' on each discovered path.
 *
 * Returns nonzero on an error.
 */
int walk_objects_by_path(struct path_walk_info *info)
{
	int ret = 0;
	size_t commits_nr = 0;
	struct commit *c;
	struct oidset added = OIDSET_INIT;
	struct string_list stack = STRING_LIST_INIT_DUP;
	size_t paths_nr = 0;
	struct type_and_oid_list *list;
	struct strmap paths_to_lists = STRMAP_INIT;
	struct progress *progress = NULL;

	trace2_region_enter("path-walk", "commit-walk", info->revs->repo);

	if (info->progress)
		progress = start_progress("Exploring commit history", 0);

	/* Insert a single list for the root tree into the paths. */
	CALLOC_ARRAY(list, 1);
	list->type = OBJ_TREE;
	strmap_put(&paths_to_lists, "", list);

	if (prepare_revision_walk(info->revs))
		die(_("failed to setup revision walk"));

	while ((c = get_revision(info->revs)))
	{
		struct object_id *oid = get_commit_tree_oid(c);
		struct tree *t = lookup_tree(info->revs->repo, oid);

		if (t && (c->object.flags & UNINTERESTING))
			t->object.flags |= UNINTERESTING;

		display_progress(progress, ++commits_nr);
		oid_array_append(&list->oids, oid);
	}

	stop_progress(&progress);
	trace2_data_intmax("backfill", the_repository, "commits", commits_nr);
	trace2_region_leave("path-walk", "commit-walk", info->revs->repo);

	string_list_append(&stack, "");

	if (info->progress)
		progress = start_progress("Exploring paths", 0);

	display_progress(progress, 0);

	while (!ret && stack.nr) {
		char *path = stack.items[stack.nr - 1].string;
		stack.nr--;

		ret = walk_path(info,
				path,
				&paths_to_lists,
				&stack,
				&added);

		display_progress(progress, ++paths_nr);
		free(path);
	}

	stop_progress(&progress);
	clear_strmap(&paths_to_lists);
	oidset_clear(&added);
	string_list_clear(&stack, 0);
	return ret;
}