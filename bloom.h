#ifndef BLOOM_H
#define BLOOM_H

struct commit;
struct repository;

struct bloom_filter_settings {
	uint32_t hash_version;
	uint32_t num_hashes;
	uint32_t bits_per_entry;
};

#define DEFAULT_BLOOM_FILTER_SETTINGS { 1, 7, 10 }

/*
 * A bloom_filter struct represents a data segment to
 * use when testing hash values. The 'len' member
 * dictates how many uint64_t entries are stored in
 * 'data'.
 */
struct bloom_filter {
	uint64_t *data;
	int len;
};

/*
 * A bloom_key represents the k hash values for a
 * given hash input. These can be precomputed and
 * stored in a bloom_key for re-use when testing
 * against a bloom_filter.
 */
struct bloom_key {
	uint32_t *hashes;
};

void load_bloom_filters(void);

struct bloom_filter *get_bloom_filter(struct repository *r,
				      struct commit *c,
				      int compute_if_null);

void fill_bloom_key(const char *data,
		    int len,
		    struct bloom_key *key,
		    struct bloom_filter_settings *settings);

#endif
