#include "git-compat-util.h"
#include "bloom.h"
#include "commit.h"
#include "commit-slab.h"
#include "commit-graph.h"
#include "object-store.h"
#include "diff.h"
#include "diffcore.h"
#include "revision.h"

#define BITS_PER_BLOCK 64

define_commit_slab(bloom_filter_slab, struct bloom_filter);

struct bloom_filter_slab bloom_filters;

static uint32_t rotate_right(uint32_t value, int32_t count)
{
	uint32_t mask = 8 * sizeof(uint32_t) - 1;
        count &= mask;
        return ((value >> count) | (value << ((-count) & mask)));
}

static uint32_t seed_murmur3(uint32_t seed, const char *data, int len)
{
	const uint32_t c1 = 0xcc9e2d51;
        const uint32_t c2 = 0x1b873593;
        const int32_t r1 = 15;
        const int32_t r2 = 13;
        const uint32_t m = 5;
        const uint32_t n = 0xe6546b64;
	int i;
	uint32_t k1 = 0;
	const char *tail;
                    
	int len4 = len / sizeof(uint32_t);

        const uint32_t *blocks = (const uint32_t*)data;

        uint32_t k;
        for (i = 0; i < len4; i++)
        {
        	k = blocks[i];
                k *= c1;
                k = rotate_right(k, r1);
                k *= c2;

                seed ^= k;
                seed = rotate_right(seed, r2) * m + n;
        }

        tail = (data + len4 * sizeof(uint32_t));

        switch (len & (sizeof(uint32_t) - 1))
        {
        case 3:
        	k1 ^= ((uint32_t)tail[2]) << 16;
		/*-fallthrough*/
        case 2:
                k1 ^= ((uint32_t)tail[1]) << 8;
		/*-fallthrough*/
        case 1:
                k1 ^= ((uint32_t)tail[0]) << 0;
                k1 *= c1;
                k1 = rotate_right(k1, r1);
                k1 *= c2;
                seed ^= k1;
                break;
        }

        seed ^= (uint32_t)len;
        seed ^= (seed >> 16);
        seed *= 0x85ebca6b;
        seed ^= (seed >> 13);
        seed *= 0xc2b2ae35;
        seed ^= (seed >> 16);

	return seed;
}

static inline uint64_t get_bitmask(uint32_t pos)
{
	return 1UL << (pos & (BITS_PER_BLOCK - 1));
}

int bloom_filter_contains(struct bloom_filter *filter,
			  struct bloom_key *key,
			  struct bloom_filter_settings *settings)
{
	int i;
	uint64_t mod = filter->len * BITS_PER_BLOCK;

	if (!mod)
		return 1;

	for (i = 0; i < settings->num_hashes; i++) {
		uint64_t hash_mod = key->hashes[i] % mod;
		uint64_t block_pos = hash_mod / BITS_PER_BLOCK;
		if (!(filter->data[block_pos] & get_bitmask(hash_mod)))
			return 0;
	}

	return 1;
}

void fill_bloom_key(const char *data,
		    int len,
		    struct bloom_key *key,
		    struct bloom_filter_settings *settings)
{
	int i;
	uint32_t seed0 = 0x293ae76f;
	uint32_t seed1 = 0x7e646e2c;

	uint32_t hash0 = seed_murmur3(seed0, data, len);
	uint32_t hash1 = seed_murmur3(seed1, data, len);

	key->hashes = (uint32_t *)xcalloc(settings->num_hashes, sizeof(uint32_t));
	for (i = 0; i < settings->num_hashes; i++) {
		key->hashes[i] = hash0 + i * hash1;
	}
}

static void add_key_to_filter(struct bloom_key *key,
			      struct bloom_filter *filter,
			      struct bloom_filter_settings *settings)
{
	int i;
	uint64_t mod = filter->len * BITS_PER_BLOCK;

	for (i = 0; i < settings->num_hashes; i++) {
		uint64_t hash_mod = key->hashes[i] % mod;
		uint64_t block_pos = hash_mod / BITS_PER_BLOCK;

		filter->data[block_pos] |= get_bitmask(hash_mod);
	}
}

static void fill_filter_from_graph(struct commit_graph *g,
				   struct bloom_filter *filter,
				   struct commit *c)
{
	uint32_t prev_index = 0;
	uint32_t next_index = get_be32(g->chunk_bloom_indexes + 4 * c->graph_pos);

	if (c->graph_pos > 0) {
		prev_index = get_be32(g->chunk_bloom_indexes + 4 * (c->graph_pos - 1));
	}

	filter->len = next_index - prev_index;
	filter->data = (uint64_t *)(g->chunk_bloom_data + 8 * prev_index + 12);
}

void load_bloom_filters(void)
{
	init_bloom_filter_slab(&bloom_filters);
}

struct bloom_filter *get_bloom_filter(struct repository *r,
				      struct commit *c,
				      int compute_if_null)
{
	struct bloom_filter *filter;
	struct bloom_filter_settings settings = DEFAULT_BLOOM_FILTER_SETTINGS;
	int already_computed = 0;

	filter = bloom_filter_slab_at(&bloom_filters, c);

	if (!filter->data) {
		load_commit_graph_info(r, c);
		if (c->graph_pos != COMMIT_NOT_FROM_GRAPH) {
			if (r->objects->commit_graph->chunk_bloom_indexes) {
				fill_filter_from_graph(r->objects->commit_graph, filter, c);
				already_computed = 1;
			}
		}
	}

	if (!filter->data && compute_if_null) {

		if (!already_computed) {
			int i;
			struct rev_info revs;
			const char *revs_argv[] = {NULL, "HEAD", NULL};
			init_revisions(&revs, NULL);
			revs.diffopt.flags.recursive = 1;
			setup_revisions(2, revs_argv, &revs, NULL);

			if (c->parents)
				diff_tree_oid(&c->parents->item->object.oid, &c->object.oid, "", &revs.diffopt);
			else
				diff_tree_oid(NULL, &c->object.oid, "", &revs.diffopt);
			diffcore_std(&revs.diffopt);

			if (diff_queued_diff.nr <= 512) {
				filter->len = (diff_queued_diff.nr * settings.bits_per_entry + BITS_PER_BLOCK - 1)
					      / BITS_PER_BLOCK;
				filter->data = xcalloc(sizeof(uint64_t), filter->len);

				for (i = 0; i < diff_queued_diff.nr; i++) {
					const char *path = diff_queued_diff.queue[i]->two->path;
					const char *p = path;

					/*
					 * Add each leading directory of the changed file, i.e. for
					 * 'dir/subdir/file' add 'dir' and 'dir/subdir' as well, so
					 * the Bloom filter could be used to speed up commands like
					 * 'git log dir/subdir', too.
					 *
					 * Note that directories are added without the trailing '/'.
					 */
					do {
						struct bloom_key key;
						fill_bloom_key(p, strlen(p), &key, &settings);
						add_key_to_filter(&key, filter, &settings);

						p = strchrnul(p + 1, '/');
					} while (*p);

					diff_free_filepair(diff_queued_diff.queue[i]);
				}
			} else {
				filter->data = NULL;
				filter->len = 0;
			}

			free(diff_queued_diff.queue);
			DIFF_QUEUE_CLEAR(&diff_queued_diff);
		}
	}

	return filter;
}

//void set_bloom_filter(struct commit *c, struct bloom_filter *filter)
//{
//	struct bloom_filter *current = bloom_filter_slab_at(&bloom_filters, c);
//
//	free(current->data);
//	current->data = filter->data;
//	current->len = filter->len;
//}
