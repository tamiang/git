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

static void insert_name_and_oid(struct names_and_oids *no,
			 const char *name,
			 const struct object_id *oid)
{
	struct string_list_item *item = string_list_insert(&no->list, name);
	struct oidset *set; 
	
	if (!item->util) {
		set = xcalloc(1, sizeof(struct oidset));
		oidset_init(set, 16);
		item->util = set;
	} else {
		set = item->util;
	}

	oidset_insert(set, oid);
}

static  void init_names_and_oids(struct names_and_oids *no) {
	string_list_init(&no->list, 1);
}

static void free_names_and_oids(struct names_and_oids *no) {
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

int num_walked = 0;
static void walk_tree_contents(struct repository *r,
			       struct tree *tree,
			       struct names_and_oids *no)
{
	struct tree_desc desc;
	struct name_entry entry;

	if (parse_tree_gently(tree, 1) < 0)
		return;

	num_walked++;

	init_tree_desc(&desc, tree->buffer, tree->size);
	while (tree_entry(&desc, &entry)) {
		switch (object_type(entry.mode)) {
		case OBJ_TREE:
			insert_name_and_oid(no, entry.path, entry.oid);

			if (tree->object.flags & UNINTERESTING)
				mark_tree_uninteresting_shallow(lookup_tree(r, entry.oid));
			break;
		case OBJ_BLOB:
			if (tree->object.flags & UNINTERESTING)
				mark_blob_uninteresting(lookup_blob(r, entry.oid));
			break;
		default:
			/* Subproject commit - not in this repository */
			break;
		}
	}

	free_tree_buffer(tree);
}

static void tree_walk_sparse(struct rev_info *revs,
	       		     struct oidset *set)
{
	int i, has_interesting = 0, has_uninteresting = 0;
	struct names_and_oids no;
	struct object_id *oid;
	struct oidset_iter iter;
	init_names_and_oids(&no);

	/* Check if we need to recurse down these trees */
	oidset_iter_init(set, &iter);
	while ((oid = oidset_iter_next(&iter))) {
		struct tree *tree = lookup_tree(revs->repo, oid);
		if (tree->object.flags & UNINTERESTING)
			has_uninteresting = 1;
		else
			has_interesting = 1;
	}

	if (!has_interesting || !has_uninteresting) {
		return;
	}

	/* Phase 1: read all trees in the set, add trees to dictionary */
	oidset_iter_init(set, &iter);
	while ((oid = oidset_iter_next(&iter))) {
		struct tree *tree = lookup_tree(revs->repo, oid);
		walk_tree_contents(revs->repo, tree, &no);
	}

	/* Phase 2: for each path, recurse on that oidset */
	for (i = 0; i < no.list.nr; i++)
		tree_walk_sparse(revs, (struct oidset *)no.list.items[i].util);

	free_names_and_oids(&no);
}

static void mark_edge_parents_uninteresting(struct commit *commit,
					    struct rev_info *revs,
					    show_edge_fn show_edge,
					    struct oidset *set)
{
	struct commit_list *parents;

	for (parents = commit->parents; parents; parents = parents->next) {
		struct commit *parent = parents->item;
		struct tree *tree;

		if (!(parent->object.flags & UNINTERESTING))
			continue;

		tree = get_commit_tree(parent);
		oidset_insert(set, &tree->object.oid);
		mark_tree_uninteresting_shallow(tree);

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
	struct oidset set;
	oidset_init(&set, 16);

	for (list = revs->commits; list; list = list->next) {
		struct commit *commit = list->item;
		oidset_insert(&set, get_commit_tree_oid(commit));

		if (commit->object.flags & UNINTERESTING) {
			mark_tree_uninteresting_shallow(get_commit_tree(commit));

			if (revs->edge_hint_aggressive && !(commit->object.flags & SHOWN)) {
				commit->object.flags |= SHOWN;
				show_edge(commit);
			}
			continue;
		}

		/* send tree list here */
		mark_edge_parents_uninteresting(commit, revs, show_edge, &set);
	}

	tree_walk_sparse(revs, &set);
	oidset_clear(&set);

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
	
	fprintf(stderr, "num_walked: %d\n", num_walked);
}
