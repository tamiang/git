#include "git-compat-util.h"
#include "cache.h"
#include "tag.h"
#include "commit.h"
#include "tree.h"
#include "blob.h"
#include "diff.h"
#include "oidset.h"
#include "tree-walk.h"
#include "revision.h"
#include "object-store.h"
#include "string-list.h"
#include "trace.h"
#include "tree-walk-sparse.h"

/*
 * At each "level" of the search, we will store a dictionary.
 *
 * Key: the entry name from a tree above to a tree in the next level.
 * Value: An oidset of tree oids that appear at that entry name.
 *
 * We use string_list as the dictionary.
 */
struct names_and_oids {
	struct string_list list;
};

void insert_name_and_oid(struct names_and_oids *no,
			 const char *name,
			 struct tree *tree)
{
	struct string_list_item *item = string_list_insert(&no->list, name)->util;
	struct oidset *set; 
	
	if (!item->util) {
		set = xcalloc(1, sizeof(struct oidset));
		oidset_init(set, 16);
		item->util = set;
	} else {
		set = item->util;
	}

	oidset_insert(set, &tree->object.oid);
}

void init_names_and_oids(struct names_and_oids *no) {
	string_list_init(&no->list, 1);
}

void free_names_and_oids(struct names_and_oids *no) {
	int i; 
	for (i = 0; i < no->list.nr; i++) {
		oidset_clear(no->list.items[i].util);
		free(no->list.items[i].util); 
	}  

	string_list_clear(&no->list, 0);
}

static void mark_blob_uninteresting(struct blob *obj)
{
	obj->object.flags |= UNINTERESTING;
}

static void mark_tree_uninteresting_shallow(struct tree *tree)
{
	struct object *obj;

	if (!tree)
		return;

	obj = &tree->object;
	if (obj->flags & UNINTERESTING)
		return;

	obj->flags |= UNINTERESTING;
	/* don't recurse now! */
}

static void mark_tree_contents_uninteresting(struct repository *r,
					     struct tree *tree)
{
	struct tree_desc desc;
	struct name_entry entry;

	if (parse_tree_gently(tree, 1) < 0)
		return;

	init_tree_desc(&desc, tree->buffer, tree->size);
	while (tree_entry(&desc, &entry)) {
		switch (object_type(entry.mode)) {
		case OBJ_TREE:
			/* add to dictionary here! */
			mark_tree_uninteresting_shallow(lookup_tree(r, entry.oid));
			break;
		case OBJ_BLOB:
			mark_blob_uninteresting(lookup_blob(r, entry.oid));
			break;
		default:
			/* Subprojct commit - not in this repository */
			break;
		}
	}

	free_tree_buffer(tree);
}

static void mark_edge_parents_uninteresting(struct commit *commit,
					    struct rev_info *revs,
					    show_edge_fn show_edge)
{
	struct commit_list *parents;

	for (parents = commit->parents; parents; parents = parents->next) {
		struct commit *parent = parents->item;

		if (!(parent->object.flags & UNINTERESTING))
			continue;
		mark_tree_uninteresting_shallow(get_commit_tree(parent));

		if (!(parent->object.flags & SHOWN)) {
			parent->object.flags |= SHOWN;
			show_edge(parent);
		}
	}
}

void mark_edges_uninteresting_sparse(struct rev_info *revs,
				     show_edge_fn show_edge)
{
	struct commit_list *list;
	int i;

	for (list = revs->commits; list; list = list->next) {
		struct commit *commit = list->item;

		if (commit->object.flags & UNINTERESTING) {
			mark_tree_uninteresting_shallow(get_commit_tree(commit));

			/* TODO: add tree to list */

			if (revs->edge_hint_aggressive && !(commit->object.flags & SHOWN)) {
				commit->object.flags |= SHOWN;
				show_edge(commit);
			}
			continue;
		}

		/* send tree list here */
		mark_edge_parents_uninteresting(commit, revs, show_edge);
	}

	/* TODO: mark the trees uninteresting using the sparse algorithm */

	if (revs->edge_hint_aggressive) {
		for (i = 0; i < revs->cmdline.nr; i++) {
			struct object *obj = revs->cmdline.rev[i].item;
			struct commit *commit = (struct commit *)obj;
			if (obj->type != OBJ_COMMIT || !(obj->flags & UNINTERESTING))
				continue;
			
			/* this will do a full recursion on the trees, stopping only
			 * at trees that are already marked UNINTERESTING. */
			mark_tree_uninteresting(revs->repo, get_commit_tree(commit));
			if (!(obj->flags & SHOWN)) {
				obj->flags |= SHOWN;
				show_edge(commit);
			}
		}
	}
}
