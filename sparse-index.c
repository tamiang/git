#include "cache.h"
#include "config.h"
#include "dir.h"
#include "hashmap.h"
#include "sparse-index.h"
#include "tree.h"
#include "pathspec.h"
#include "tree-walk.h"

static char *get_sparse_checkout_filename(void)
{
	return git_pathdup("info/sparse-checkout");
}

static int get_sparse_checkout_patterns(struct pattern_list *pl)
{
	int res;
	char *sparse_filename = get_sparse_checkout_filename();

	res = add_patterns_from_file_to_list(sparse_filename, "", 0, pl, NULL);

	free(sparse_filename);
	return res;
}

static int hashmap_contains_path(struct hashmap *map,
				 struct strbuf *pattern)
{
	struct pattern_entry p;

	/* Check straight mapping */
	p.pattern = pattern->buf;
	p.patternlen = pattern->len;
	hashmap_entry_init(&p.ent,
			   ignore_case ?
			   strihash(p.pattern) :
			   strhash(p.pattern));
	return !!hashmap_get_entry(map, &p, ent, NULL);
}

static char *get_sparse_dir_name(struct pattern_list *pl,
				 const char *sparse_path)
{
	struct strbuf path = STRBUF_INIT;
	struct strbuf parent = STRBUF_INIT;
	const char *slash_pos;

	strbuf_addstr(&path, sparse_path);
	slash_pos = strrchr(path.buf, '/');

	if (slash_pos == path.buf)
		BUG("path at root should not be sparse in cone mode");

	/* definitely remove the filename from the path */
	strbuf_setlen(&path, slash_pos - path.buf);

	/* copy to the parent, then extract another directory */
	strbuf_add(&parent, path.buf, slash_pos - path.buf);
	slash_pos = strrchr(parent.buf, '/');

	while (slash_pos > parent.buf) {
		strbuf_setlen(&parent, slash_pos - parent.buf);

		/*
		 * If the parent is in the parent_hashmap, then we
		 * found the boundary where 'parent' is included but
		 * everything under 'path' is sparse.
		 */
		if (hashmap_contains_path(&pl->parent_hashmap, &parent))
			break;

		/* pop last path entry off 'path' */
		strbuf_setlen(&path, parent.len);

		/* pop last path entry off 'parent' */
		slash_pos = strrchr(parent.buf, '/');
	}

	strbuf_release(&parent);
	strbuf_addch(&path, '/');

	return strbuf_detach(&path, NULL);
}

#define DIR_MODE 0100

static struct cache_entry *construct_sparse_dir_entry(
				struct index_state *istate,
				struct pattern_list *pl,
				int start, int *end)
{
	struct cache_entry *de;
	struct object_id tree_oid;
	struct strbuf HEAD_colon_tree = STRBUF_INIT;
	char *sparse_dir = get_sparse_dir_name(pl, istate->cache[start]->name);
	*end = start + 1;

	while (*end < istate->cache_nr &&
	       starts_with(istate->cache[*end]->name, sparse_dir))
		(*end)++;

	strbuf_addf(&HEAD_colon_tree, "HEAD:%s", sparse_dir);
	if (get_oid(HEAD_colon_tree.buf, &tree_oid))
		BUG("sparse-index cannot handle missing sparse directories");

	de = make_cache_entry(istate, DIR_MODE, &tree_oid, sparse_dir, 0, 0);

	de->ce_flags |= CE_SKIP_WORKTREE;
	strbuf_release(&HEAD_colon_tree);
	free(sparse_dir);
	return de;
}

int convert_to_sparse(struct index_state *istate)
{
	int i, cur_i = 0;
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

	istate->drop_cache_tree = 1;
	istate->sparse_index = 1;

	for (i = 0; i < istate->cache_nr; cur_i++) {
		int end;
		struct cache_entry *ce = istate->cache[i];

		/* if not sparse, copy the entry and move forward */
		if (!(ce->ce_flags & CE_SKIP_WORKTREE)) {
			istate->cache[cur_i] = ce;
			i++;
			continue;
		}

		/*
		 * We entered a sparse region. Discover the highest path
		 * that does not match a parent pattern, and insert that
		 * into the index as a sparse directory. Then skip all
		 * entries that match that leading directory.
		 */
		istate->cache[cur_i] = construct_sparse_dir_entry(istate, &pl, i, &end);
		discard_cache_entry(ce);

		while (++i < end)
			discard_cache_entry(istate->cache[i]);
	}

	istate->cache_nr = cur_i;

	clear_pattern_list(&pl);
	return 0;
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
	hashmap_clear_and_free(&istate->name_hash, struct cache_entry, ent);
	memcpy(&istate->name_hash, &full->name_hash, sizeof(full->name_hash));
	istate->sparse_index = 0;
	istate->cache = full->cache;
	istate->cache_nr = full->cache_nr;
	istate->cache_alloc = full->cache_alloc;

	free(full);

	trace2_region_leave("index", "ensure_full_index", r);
	return 0;
}

