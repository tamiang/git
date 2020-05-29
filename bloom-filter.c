#include "bloom-filter.h"

void bloom_filter_init(struct bloom_filter *bf, unsigned int nr_hashes,
		       uint32_t nr_elements)
{
	/* n * k / ln(2) â‰ˆ n * k / 0.69315 â‰ˆ n * k * 10 / 7 */
	uint32_t nr_bits = st_mult(st_mult(nr_elements, nr_hashes), 10) / 7;
	uint32_t nr_bytes = st_add(nr_bits, 7) / 8;
	/*
	 * But we round up to fully utilize all bytes, thus lowering the
	 * probability of false positives a bit.
	 */
	bf->nr_bits = nr_bytes * 8;
	bf->bits = xcalloc(nr_bytes, sizeof(*(bf->bits)));
}

void bloom_filter_init_with_size(struct bloom_filter *bf, uint32_t nr_bits)
{
	uint32_t nr_bytes = st_add(nr_bits, 7) / 8;
	bf->nr_bits = nr_bits;
	bf->bits = xcalloc(nr_bytes, sizeof(*(bf->bits)));
}

void bloom_filter_free(struct bloom_filter *bf)
{
	FREE_AND_NULL(bf->bits);
	bf->nr_bits = 0;
}

uint32_t bloom_filter_bytes(struct bloom_filter *bf)
{
	return (bf->nr_bits + 7) / 8;
}

void bloom_filter_clear_all_bits(struct bloom_filter *bf)
{
	memset(bf->bits, 0, bloom_filter_bytes(bf));
}

void bloom_filter_set_all_bits(struct bloom_filter *bf)
{
	memset(bf->bits, 0xff, bloom_filter_bytes(bf));
}

static inline uint32_t bit_offset(uint32_t nr_bits, uint32_t hash)
{
	return hash % nr_bits;
}

static inline uint32_t byte_offset(uint32_t nr_bits, uint32_t bit_offset)
{
	return (nr_bits - 1) / 8 - bit_offset / 8;
}

static inline uint8_t byte_mask(uint32_t bit_offset)
{
	return 1 << (bit_offset % 8);
}

static inline void bloom_filter_set_one_bit(struct bloom_filter *bf,
					    uint32_t hash)
{
	uint32_t offset = bit_offset(bf->nr_bits, hash);
	bf->bits[byte_offset(bf->nr_bits, offset)] |= byte_mask(offset);
}

void bloom_filter_set_bits(struct bloom_filter *bf, const uint32_t *hashes,
			   unsigned int nr_hashes)
{
	unsigned int i;
	for (i = 0; i < nr_hashes; i++)
		bloom_filter_set_one_bit(bf, hashes[i]);
}

static inline int bloom_filter_check_one_bit(struct bloom_filter *bf,
					     uint32_t hash)
{
	uint32_t offset = bit_offset(bf->nr_bits, hash);
	return bf->bits[byte_offset(bf->nr_bits, offset)] & byte_mask(offset);
}

enum bloom_result bloom_filter_check_bits(struct bloom_filter *bf,
					  const uint32_t *hashes,
					  unsigned int nr_hashes)
{
	unsigned int i;
	for (i = 0; i < nr_hashes; i++)
		if (!bloom_filter_check_one_bit(bf, hashes[i]))
			return BLOOM_DEFINITELY_NOT;
	return BLOOM_POSSIBLY_YES;
}
