#ifndef BLOOM_FILTER_H
#define BLOOM_FILTER_H

#include "git-compat-util.h"

enum bloom_result {
	/*
	 * BLOOM_CANT_TELL is a value that a caller can use to report that
	 * a Bloom filter is not available; bloom_filter_check_bits() will
	 * never return it.
	 */
	BLOOM_CANT_TELL = -1,
	BLOOM_DEFINITELY_NOT = 0,
	BLOOM_POSSIBLY_YES = 1
};

struct bloom_filter {
	uint32_t nr_bits;
	uint8_t *bits;
};

/*
 * Initialize a Bloom filter with the number of bits that is (close to)
 * optimal to hold the given number of elements using the given number
 * of hashes per element.
 */
void bloom_filter_init(struct bloom_filter *bf, uint32_t nr_hashes,
		       uint32_t nr_elements);

/* Initialize a Bloom filter with the given number of bits */
void bloom_filter_init_with_size(struct bloom_filter *bf, uint32_t nr_bits);

void bloom_filter_free(struct bloom_filter *bf);

/* Return the size of the Bloom filter's bit array in bytes */
uint32_t bloom_filter_bytes(struct bloom_filter *bf);

void bloom_filter_clear_all_bits(struct bloom_filter *bf);
void bloom_filter_set_all_bits(struct bloom_filter *bf);

void bloom_filter_set_bits(struct bloom_filter *bf, const uint32_t *hashes,
			   unsigned int nr_hashes);
enum bloom_result bloom_filter_check_bits(struct bloom_filter *bf,
					  const uint32_t *hashes,
					  unsigned int nr_hashes);

#endif
