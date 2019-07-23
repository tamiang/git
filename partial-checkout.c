#include "cache.h"
#include "config.h"
#include "dir.h"
#include "hashmap.h"
#include "repository.h"
#include "run-command.h"
#include "partial-checkout.h"

static struct strbuf partial_checkout_data = STRBUF_INIT;

/*
 * Stores paths where everything starting with those paths
 * is included.
 */
static struct hashmap pc_recursive_hashmap;

/*
 * Stores paths that are parents of recursive hashmap paths.
 * Used to check single-level parents of blobs.
 */
static struct hashmap pc_parents_hashmap;

int core_partial_checkout = -1;

int use_partial_checkout(struct repository *r)
{
	if (core_partial_checkout >= 0)
		return core_partial_checkout;

	if (repo_config_get_bool(r, "core.partialcheckout", &core_partial_checkout))
		core_partial_checkout = 0;

	return core_partial_checkout;
}

char *get_partial_checkout_filename(struct repository *r)
{
	struct strbuf pc_filename = STRBUF_INIT;
	strbuf_addstr(&pc_filename, r->gitdir);
	strbuf_addstr(&pc_filename, "/info/partial-checkout");
	return strbuf_detach(&pc_filename, NULL);
}

struct partial_checkout_entry {
	struct hashmap_entry ent; /* must be the first member! */
	const char *pattern;
	int patternlen;
};

static unsigned int(*pc_hash)(const void *buf, size_t len);
static int(*pc_cmp)(const char *a, const char *b, size_t len);

static int pc_hashmap_cmp(const void *unused_cmp_data,
	const void *a, const void *b, const void *key)
{
	const struct partial_checkout_entry *pc_1 = a;
	const struct partial_checkout_entry *pc_2 = b;

	return pc_cmp(pc_1->pattern, pc_2->pattern, pc_1->patternlen);
}

void get_partial_checkout_data(struct repository *r,
			       struct strbuf *pc_data)
{
	char *pc_filename = get_partial_checkout_filename(r);

	strbuf_read_file(pc_data, pc_filename, 0);

	free(pc_filename);
}

static int check_hashmap(struct hashmap *map,
			 const char *pattern,
			 int patternlen)
{
	struct strbuf sb = STRBUF_INIT;
	struct partial_checkout_entry pc;

	/* Check straight mapping */
	strbuf_reset(&sb);
	strbuf_add(&sb, pattern, patternlen);
	pc.pattern = sb.buf;
	pc.patternlen = sb.len;
	hashmap_entry_init(&pc, pc_hash(pc.pattern, pc.patternlen));
	if (hashmap_get(map, &pc, NULL)) {
		strbuf_release(&sb);
		return 1;
	}

	strbuf_release(&sb);
	return 0;
}
static int check_recursive_hashmap(struct hashmap *map,
				   const char *pattern,
				   int patternlen)
{
	struct strbuf sb = STRBUF_INIT;
	struct partial_checkout_entry pc;
	char *slash;

	/* Check straight mapping */
	strbuf_reset(&sb);
	strbuf_add(&sb, pattern, patternlen);
	pc.pattern = sb.buf;
	pc.patternlen = sb.len;
	hashmap_entry_init(&pc, pc_hash(pc.pattern, pc.patternlen));
	if (hashmap_get(map, &pc, NULL)) {
		strbuf_release(&sb);
		return 1;
	}

	/*
	 * Check to see if it matches a directory or any path
	 * underneath it.  In other words, 'a/b/foo.txt' will match
	 * '/', 'a/', and 'a/b/'.
	 */
	slash = strchr(sb.buf, '/');

	/* include all values at root */
	if (!slash) {
		strbuf_release(&sb);
		return 1;
	}

	while (slash) {
		pc.pattern = sb.buf;
		pc.patternlen = slash - sb.buf;
		hashmap_entry_init(&pc, pc_hash(pc.pattern, pc.patternlen));
		if (hashmap_get(map, &pc, NULL)) {
			strbuf_release(&sb);
			return 1;
		}
		slash = strchr(slash + 1, '/');
	}

	strbuf_release(&sb);
	return 0;
}

static void includes_hashmap_add(struct hashmap *map, const char *pattern, const int patternlen)
{
	struct partial_checkout_entry *pc;

	pc = xmalloc(sizeof(struct partial_checkout_entry));
	pc->pattern = pattern;
	pc->patternlen = patternlen;
	hashmap_entry_init(pc, pc_hash(pc->pattern, pc->patternlen));
	hashmap_add(map, pc);
}

static void initialize_includes_hashmap(struct hashmap *map, struct strbuf *pc_data)
{
	char *buf, *entry;
	size_t len;
	int i;

	/*
	 * Build a hashmap of the partial-checkout data we can use to look
	 * for cache entry matches quickly
	 */
	pc_hash = ignore_case ? memihash : memhash;
	pc_cmp = ignore_case ? strncasecmp : strncmp;
	hashmap_init(map, pc_hashmap_cmp, NULL, 0);

	entry = buf = pc_data->buf;
	len = pc_data->len;
	for (i = 0; i < len; i++) {
		if (buf[i] == '\n') {
			buf[i] = '\0';
			includes_hashmap_add(map, entry, buf + i - entry);
			entry = buf + i + 1;
		}
	}
}

static void pc_parents_hashmap_add(struct hashmap *map, const char *pattern, const int patternlen)
{
	char *slash;
	struct partial_checkout_entry *pc;

	/*
	 * Add any directories leading up to the file as the excludes logic
	 * needs to match directories leading up to the files as well. Detect
	 * and prevent unnecessary duplicate entries which will be common.
	 */
	if (patternlen > 1) {
		slash = strchr(pattern + 1, '/');
		while (slash) {
			struct strbuf added = STRBUF_INIT;
			pc = xmalloc(sizeof(struct partial_checkout_entry));
			pc->pattern = pattern;
			pc->patternlen = slash - pattern;
			hashmap_entry_init(pc, pc_hash(pc->pattern, pc->patternlen));
			strbuf_add(&added, pc->pattern, pc->patternlen);
			
			if (hashmap_get(map, pc, NULL))
				free(pc);
			else
				hashmap_add(map, pc);
			slash = strchr(slash + 1, '/');
		}
	}
}

static void initialize_parents_hashmap(struct hashmap *map, struct strbuf *pc_data)
{
	char *buf, *entry;
	size_t len;
	int i;

	/*
	 * Build a hashmap of the parent directories contained in the virtual
	 * file system data we can use to look for matches quickly
	 */
	pc_hash = ignore_case ? memihash : memhash;
	pc_cmp = ignore_case ? strncasecmp : strncmp;
	hashmap_init(map, pc_hashmap_cmp, NULL, 0);

	entry = buf = pc_data->buf;
	len = pc_data->len;
	for (i = 0; i < len; i++) {
		if (buf[i] == '\0') {
			pc_parents_hashmap_add(map, entry, buf + i - entry);
			entry = buf + i + 1;
		}
	}
}

/*
 * Return 1 if the requested item is found in the partial-checkout file,
 * 0 for not found and -1 for undecided.
 */
int is_included_in_partial_checkout(struct repository *r,
				    const char *pathname, int pathlen)
{
	int result;
	struct strbuf parent_pathname = STRBUF_INIT;

	if (!core_partial_checkout)
		return -1;

	if (!pc_recursive_hashmap.tablesize && partial_checkout_data.len) {
		initialize_includes_hashmap(&pc_recursive_hashmap, &partial_checkout_data);
		initialize_parents_hashmap(&pc_parents_hashmap, &partial_checkout_data);
	}
	if (!pc_recursive_hashmap.tablesize)
		return -1;
	
	result = check_recursive_hashmap(&pc_recursive_hashmap, pathname, pathlen);

	if (result)
		return result;

	strbuf_add(&parent_pathname, pathname, pathlen);
	parent_pathname.len = (int)(strrchr(parent_pathname.buf, '/') - parent_pathname.buf);
	strbuf_setlen(&parent_pathname, parent_pathname.len);

	result = check_hashmap(&pc_parents_hashmap,
			       parent_pathname.buf,
			       parent_pathname.len);

	strbuf_release(&parent_pathname);
	return result;
}

void apply_partial_checkout(struct repository *r, struct index_state *istate)
{
	int i;

	if (!use_partial_checkout(r))
		return;

	if (!partial_checkout_data.len)
		get_partial_checkout_data(r, &partial_checkout_data);

	for (i = 0; i < istate->cache_nr; i++) {
		if (is_included_in_partial_checkout(r,
						    istate->cache[i]->name,
						    istate->cache[i]->ce_namelen))
			istate->cache[i]->ce_flags &= ~CE_SKIP_WORKTREE;
		else
			istate->cache[i]->ce_flags |= CE_SKIP_WORKTREE;
	}
}

/*
 * Free the partial-checkout file data structures.
 */
void free_partial_checkout(struct repository *r) {
	hashmap_free(&pc_recursive_hashmap, 1);
	hashmap_free(&pc_parents_hashmap, 1);
	strbuf_release(&partial_checkout_data);
}