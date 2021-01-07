#include "cache.h"
#include "config.h"
#include "dir.h"
#include "hashmap.h"
#include "sparse-index.h"
#include "tree.h"
#include "pathspec.h"
#include "tree-walk.h"
#include "fsmonitor.h"
#include "cache-tree.h"

static char *get_sparse_checkout_filename(void)
{
	return git_pathdup("info/sparse-checkout");
}

static int get_sparse_checkout_patterns(struct pattern_list *pl)
{
	int res;
	char *sparse_filename = get_sparse_checkout_filename();

	pl->use_cone_patterns = core_sparse_checkout_cone;
	res = add_patterns_from_file_to_list(sparse_filename, "", 0, pl, NULL);

	free(sparse_filename);
	return res;
}

#define DIR_MODE 0100

static struct cache_entry *construct_sparse_dir_entry(
				struct index_state *istate,
				const char *sparse_dir,
				struct cache_tree *tree)
{
	struct cache_entry *de;

	de = make_cache_entry(istate, DIR_MODE, &tree->oid, sparse_dir, 0, 0);

	de->ce_flags |= CE_SKIP_WORKTREE;
	return de;
}

/*
 * Returns the number of entries "inserted" into the index.
 */
static int convert_to_sparse_rec(struct repository *repo,
				 struct index_state *istate,
				 int num_converted,
				 int start, int end,
				 const char *ct_path, size_t ct_pathlen,
				 struct cache_tree *ct,
				 struct pattern_list *pl)
{
	int i, sub, can_convert = 1;
	int start_converted = num_converted;
	enum pattern_match_result match;
	int dtype;
	struct strbuf next_subtree_match = STRBUF_INIT;

	/*
	 * Is the current path outside of the sparse cone?
	 * Then check if the region can be replaced by a sparse
	 * directory entry (everything is sparse and merged).
	 */
	match = path_matches_pattern_list(ct_path, ct_pathlen,
					  NULL, &dtype, pl, istate);
	if (match != NOT_MATCHED)
		can_convert = 0;

	for (i = start; can_convert && i < end; i++) {
		struct cache_entry *ce = istate->cache[i];

		if (ce_stage(ce) ||
		    !(ce->ce_flags & CE_SKIP_WORKTREE))
			can_convert = 0;
	}

	if (can_convert) {
		struct cache_entry *se;
		se = construct_sparse_dir_entry(istate, ct_path, ct);

		istate->cache[num_converted++] = se;
		return 1;
	}

	sub = 0;
	if (ct->subtree_nr) {
		strbuf_add(&next_subtree_match, ct_path, ct_pathlen);
		strbuf_add(&next_subtree_match, ct->down[0]->name, ct->down[0]->namelen);
		strbuf_addch(&next_subtree_match, '/');
	}

	for (i = start; i < end; ) {
		int count;
		int span;
		struct cache_entry *ce = istate->cache[i];

		/*
		 * Detect if this is a normal entry oustide of the next
		 * cache subtree entry.
		 */
		if (sub >= ct->subtree_nr ||
		    ce->ce_namelen <= next_subtree_match.len ||
		    strncmp(ce->name, next_subtree_match.buf, next_subtree_match.len)) {
			istate->cache[num_converted++] = ce;
			i++;
			continue;
		}

		span = ct->down[sub]->cache_tree->entry_count;
		count = convert_to_sparse_rec(repo, istate,
					      num_converted, i, i + span,
					      next_subtree_match.buf,
					      next_subtree_match.len,
					      ct->down[sub]->cache_tree,
					      pl);
		num_converted += count;
		i += span;
		sub++;

		if (sub < ct->subtree_nr) {
			strbuf_setlen(&next_subtree_match, ct_pathlen);
			strbuf_add(&next_subtree_match,
				   ct->down[sub]->name,
				   ct->down[sub]->namelen);
			strbuf_addch(&next_subtree_match, '/');
		}
	}

	strbuf_release(&next_subtree_match);
	return num_converted - start_converted;
}

int convert_to_sparse(struct repository *repo, struct index_state *istate)
{
	int res = 0;
	struct pattern_list pl;

	if (istate->split_index || istate->sparse_index ||
	    !core_apply_sparse_checkout || !core_sparse_checkout_cone)
		return 0;

	/*
	 * For now, only create a sparse index with the
	 * GIT_TEST_SPARSE_INDEX environment variable. We will relax
	 * this once we have a proper way to opt-in (and later still,
	 * opt-out).
	 */
	if (!git_env_bool("GIT_TEST_SPARSE_INDEX", 0))
		return 0;

	memset(&pl, 0, sizeof(pl));
	if (get_sparse_checkout_patterns(&pl))
		return 0;

	if (!pl.use_cone_patterns) {
		warning(_("attempting to use sparse-index without cone mode"));
		res = 1;
		goto done;
	}

	if (cache_tree_update(istate, 0)) {
		warning(_("unable to update cache-tree, staying full"));
		goto done;
	}

	remove_fsmonitor(istate);

	istate->cache_nr = convert_to_sparse_rec(repo, istate,
						 0, 0, istate->cache_nr,
						 "", 0, istate->cache_tree,
						 &pl);
	istate->drop_cache_tree = 1;
	istate->sparse_index = 1;

done:
	clear_pattern_list(&pl);
	return res;
}

static void set_index_entry(struct index_state *istate, int nr, struct cache_entry *ce)
{
	ALLOC_GROW(istate->cache, nr + 1, istate->cache_alloc);

	istate->cache[nr] = ce;
	add_name_hash(istate, ce);
}

/* read_tree_fn_t */
static int add_path_to_index(const struct object_id *oid,
				struct strbuf *base, const char *path,
				unsigned int mode, int stage, void *context)
{
	struct index_state *istate = (struct index_state *)context;
	struct cache_entry *ce;
	size_t len = base->len;

	if (S_ISDIR(mode))
		return READ_TREE_RECURSIVE;

	strbuf_addstr(base, path);

	ce = make_cache_entry(istate, mode, oid, base->buf, 0, 0);
	ce->ce_flags |= CE_SKIP_WORKTREE;
	set_index_entry(istate, istate->cache_nr++, ce);

	strbuf_setlen(base, len);
	return 0;
}

int ensure_full_index(struct repository *r, struct index_state *istate)
{
	struct index_state *full;
	int i;

	if (!istate) {
		repo_read_index(r);
		istate = r->index;
	}

	if (!istate->sparse_index)
		return 0;

	trace2_region_enter("index", "ensure_full_index", r);

	/* initialize basics of new index */
	full = xcalloc(1, sizeof(struct index_state));

	/* copy everything */
	memcpy(full, istate, sizeof(struct index_state));

	/* then change the necessary things */
	full->sparse_index = 0;
	full->cache_alloc = (3 * istate->cache_alloc) / 2;
	full->cache_nr = 0;
	ALLOC_ARRAY(full->cache, full->cache_alloc);

	for (i = 0; i < istate->cache_nr; i++) {
		struct cache_entry *ce = istate->cache[i];
		struct tree *tree;
		struct pathspec ps;

		if (ce->ce_mode != 01000755) {
			set_index_entry(full, full->cache_nr++, ce);
			continue;
		}
		if (!(ce->ce_flags & CE_SKIP_WORKTREE))
			warning(_("index entry is a directory, but not sparse (%08x)"),
				ce->ce_flags);

		/* recursively walk into cd->name */
		tree = lookup_tree(r, &ce->oid);

		memset(&ps, 0, sizeof(ps));
		ps.recursive = 1;
		ps.has_wildcard = 1;
		ps.max_depth = -1;

		read_tree_recursive(r, tree,
				    ce->name, strlen(ce->name),
				    0, &ps,
				    add_path_to_index, full);

		/* free directory entries. full entries are re-used */
		discard_cache_entry(ce);
	}

	/* Copy back into original index, which _could_ be the_index */
	/* TODO: extract to helper */
	/* hashmap_clear(&istate->name_hash); */
	memcpy(&istate->name_hash, &full->name_hash, sizeof(full->name_hash));
	istate->sparse_index = 0;
	istate->cache = full->cache;
	istate->cache_nr = full->cache_nr;
	istate->cache_alloc = full->cache_alloc;

	free(full);

	trace2_region_leave("index", "ensure_full_index", r);
	return 0;
}

