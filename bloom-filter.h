#ifndef BLOOM_FILTER_H
#define BLOOM_FILTER_H

#include "git-compat-util.h"

struct bloom_filter {
	uint32_t nr_entries;
	uint32_t commit_nr;
	uint32_t bit_size;
	unsigned char *bits;
};


void bloom_filter_init(struct bloom_filter *bf, uint32_t commit_nr, uint32_t bit_size);
void bloom_filter_free(struct bloom_filter *bf);

/*
 * Turns the given (SHA1) hash into 5 unsigned ints, and sets the bits at
 * those positions (modulo the bitmap's size) in the Bloom filter.
 */
void bloom_filter_add_hash(struct bloom_filter *bf, uint32_t graph_pos, const unsigned char *hash);
/*
 * Turns the given (SHA1) hash into 5 unsigned ints, and checks the bits at
 * those positions (modulo the bitmap's size) in the Bloom filter.
 * Returns 1 if all those bits are set, 0 otherwise.
 */
int bloom_filter_check_hash(struct bloom_filter *bf, uint32_t graph_pos, const unsigned char *hash);

void hashxor(const unsigned char *hash1, const unsigned char *hash2,
	     unsigned char *out);

int bloom_filter_load(struct bloom_filter *bf);
void bloom_filter_write(struct bloom_filter *bf);

#endif
