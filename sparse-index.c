#include "cache.h"
#include "config.h"
#include "dir.h"
#include "hashmap.h"
#include "sparse-index.h"

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

	fprintf(stderr, "found sparse dir at '%s'\n", path.buf);
	return strbuf_detach(&path, NULL);
}

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

	de = make_cache_entry(istate, 0100, &tree_oid, sparse_dir, 0, 0);

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
