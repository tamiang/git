#include "git-compat-util.h"
#include "alloc.h"
#include "commit.h"
#include "commit-graph.h"
#include "prio-queue.h"
#include "tree.h"
#include "revision.h"
#include "tag.h"
#include "object-reach.h"
#include "ewah/ewok.h"
#include "oid-array.h"
#include "pathspec.h"
#include "pack-bitmap.h"

/*
 * We track a global bitmap that is equal to the union of all
 * precomputed reachability bitmaps that do not contain any of
 * the given oids.
 */
static struct bitmap_index *bi;
static struct bitmap *unreachable;
static int unreachable_unions;

/* Remember to update object flag allocation in object.h */
#define CHECKED		(1u<<23)
#define CAN_REACH	(1u<<24)

static int object_checked(struct object *o)
{
	int result = 0;
	if (o->flags & CHECKED) {
		result = 1;
		goto cleanup;
	}

	if (unreachable_unions)
		result = bitmap_walk_contains(bi, unreachable, &o->oid);

cleanup:
	return result;
}

struct tree_dfs_entry {
	/* The current tree at this DFS position. */
	struct tree *tree;

	/* The child tree OIDs (we test blobs during tree parsing). */
	struct oid_array child_entries;

	/* The next child to explore during our DFS walk. */
	size_t cur_child;
};

struct tree_stack {
	size_t nr, alloc;
	struct tree_dfs_entry *stack;
};

static void push_tree_to_stack(struct tree_stack *stack, struct tree* tree)
{
	ALLOC_GROW(stack->stack, stack->nr + 1, stack->alloc);
	memset(&stack->stack[stack->nr], 0, sizeof(struct tree_dfs_entry));
	stack->stack[stack->nr].tree = tree;
	stack->nr++;
}

static void pop_tree_from_stack(struct tree_stack *stack)
{
	if (!stack->nr)
		return;
	stack->nr--;

	oid_array_clear(&stack->stack[stack->nr].child_entries);
}

struct tree_contains_context {
	struct repository *r;
	struct oid_array *objects;
	struct tree_dfs_entry *dfs;
};

static int check_and_add_tree_child(const struct object_id *oid,
			     struct strbuf *base, const char *path,
			     unsigned int mode, void *context)
{
	struct tree_contains_context *ctx = context;
	struct object *o;

	for (size_t i = 0; i < ctx->objects->nr; i++ ) {
		if (oideq(oid, &ctx->objects->oid[i])) {
			return -1;
		}
	}

	o = parse_object(ctx->r, oid);
	if (!o) {
		/* Failed to find object! */
		return 0;
	}

	if (object_checked(o) ||
	    o->type != OBJ_TREE)
		return !!(o->flags & CAN_REACH);

	oid_array_append(&ctx->dfs->child_entries, oid);
	return 0;
}

static int tree_contains(struct repository *r,
			 struct tree *tree,
			 struct oid_array *objects)
{
	int result = 0;
	struct tree_stack stack = { 0 };
	struct pathspec ps = { 0 };
	struct tree_contains_context ctx = {
		.r = r,
		.objects = objects,
	};

	ps.recursive = 1;
	ps.has_wildcard = 1;
	ps.max_depth = -1;

	if (object_checked(&tree->object))
		return !!(tree->object.flags & CAN_REACH);

	tree->object.flags |= CHECKED;

	for (size_t i = 0; i < objects->nr; i++) {
		if (oideq(&tree->object.oid, &objects->oid[i])) {
			tree->object.flags |= CAN_REACH;
			return 1;
		}
	}

	push_tree_to_stack(&stack, tree);
	ctx.dfs = &stack.stack[0];
	if (read_tree(r, tree, &ps, check_and_add_tree_child, &ctx))
		goto pop_all_trees_as_reachable;

	while (stack.nr) {
		struct tree_dfs_entry *tde = &stack.stack[stack.nr - 1];
		struct tree *next;

		if (tde->cur_child >= tde->child_entries.nr) {
			/*
			 * Pop from the stack, as we have walked all
			 * children at this point.
			 */
			pop_tree_from_stack(&stack);
			continue;
		}

		next = lookup_tree(r, &tde->child_entries.oid[tde->cur_child]);
		tde->cur_child++;

		/*
		 * Due to how we construct this list, 'next' does not
		 * have the CHECKED flag, and thus doesn't have the
		 * CAN_REACH flag.
		 */

		next->object.flags |= CHECKED;
		push_tree_to_stack(&stack, next);

		/* Initialize the list of children. */
		ctx.dfs = &stack.stack[stack.nr - 1];

		/*
		 * If the helper terminates early, then it is because
		 * we found an object or one with CAN_REACH.
		 */
		if (read_tree(r, next, &ps, check_and_add_tree_child, &ctx))
			goto pop_all_trees_as_reachable;
	}

	goto cleanup;

pop_all_trees_as_reachable:
	result = 1;
	while (stack.nr) {
		struct tree_dfs_entry *tde = &stack.stack[stack.nr - 1];

		tde->tree->object.flags |= CAN_REACH;
		pop_tree_from_stack(&stack);
	}

cleanup:
	free(stack.stack);
	return result;
}

static int commit_contains_dfs_commits(struct repository *r,
				       struct commit *commit,
				       struct oid_array *objects)
{
	struct commit_list *stack = NULL;

	/* We may have determined this earlier. */
	if (object_checked(&commit->object))
		return !!(commit->object.flags & CAN_REACH);

	/* We mark commits as CHECKED as they enter the stack. */
	commit->object.flags |= CHECKED;
	commit_list_insert(commit, &stack);

	while (stack) {
		struct commit *c = stack->item;
		repo_parse_commit(r, c);

		for (size_t i = 0; i < objects->nr; i++) {
			if (oideq(&objects->oid[i], &c->object.oid))
				goto pop_stack_reachable;
		}

		for (struct commit_list *parents = c->parents;
		     parents; parents = parents->next) {
			/*
			 * If this parent can reach, then everything in
			 * the stack can also reach.
			 */
			if (parents->item->object.flags & CAN_REACH)
				goto pop_stack_reachable;
			/* Ignore checked parents. */
			if (object_checked(&parents->item->object))
				continue;

			parents->item->object.flags |= CHECKED;
			commit_list_insert(parents->item, &stack);
			break;
		}

		/*
		 * We did not push a parent, so it is time to search
		 * this commit itself.
		 */
		if (stack->item == c) {
			struct tree *tree = get_commit_tree(c);
			if (tree_contains(r, tree, objects))
				goto pop_stack_reachable;
			pop_commit(&stack);
		}

	}

	return 0;

pop_stack_reachable:
	/*
	 * We found an object! Report everything in the DFS path
	 * as being able to reach the object to improve future
	 * iterations.
	 */
	while (stack) {
		stack->item->object.flags |= CAN_REACH;
		pop_commit(&stack);
	}

	return 1;
}

static int check_and_union_bitmap(struct bitmap_index *b, struct commit *c,
				  struct ewah_bitmap *bitmap, void *cbdata)
{
	struct oid_array *objects = cbdata;
	struct bitmap *bm = ewah_to_bitmap(bitmap);

	/* If we contain any object, skip this one. */
	for (size_t i = 0; i < objects->nr; i++) {
		if (bitmap_walk_contains(bi, bm, &objects->oid[i]))
			goto cleanup;
	}

	/*
	 * None of the objects are reachable from this bitmap, so
	 * add its bits to the unreachable bitmap.
	 */
	bitmap_or(unreachable, bm);
	unreachable_unions++;

cleanup:
	bitmap_free(bm);
	return 0;
}

static void prepare_unreachable_bitmap(struct repository *r,
				       struct oid_array *objects)
{
	if (bi)
		return;

	unreachable_unions = 0;
	bi = prepare_bitmap_git(r);
	unreachable = bitmap_new();

	for_each_commit_bitmap(r, bi, check_and_union_bitmap, objects);
}

int commit_contains_object(struct repository *r, struct commit *commit,
			   struct oid_array *objects)
{
	prepare_unreachable_bitmap(r, objects);
	return commit_contains_dfs_commits(r, commit, objects);
}

void clear_commit_contains_object_flags(struct repository *r)
{
	bitmap_free(unreachable);
	unreachable = NULL;
	free_bitmap_index(bi);
	bi = NULL;
	clear_object_flags(CHECKED | CAN_REACH);
}
