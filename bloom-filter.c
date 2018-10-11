#include "cache.h"
#include "bloom-filter.h"

void bloom_filter_init(struct bloom_filter *bf, uint32_t commit_nr, uint32_t bit_size)
{
	if (bit_size % CHAR_BIT)
		BUG("invalid size for bloom filter");
	if (bit_size > 1024)
		BUG("aborting: the bit size is per commit, not for the whole filter");

	bf->nr_entries = 0;
	bf->commit_nr = commit_nr;
	bf->bit_size = bit_size;
	bf->bits = xcalloc(1, commit_nr * bit_size / CHAR_BIT);
}

void bloom_filter_free(struct bloom_filter *bf)
{
	bf->nr_entries = 0;
	bf->bit_size = 0;
	FREE_AND_NULL(bf->bits);
}


static void bloom_filter_set_bits(struct bloom_filter *bf, uint32_t graph_pos, const uint32_t *offsets,
			   int nr_offsets, int nr_entries)
{
	int i;
	for (i = 0; i < nr_offsets; i++) {
		uint32_t byte_offset = (offsets[i] % bf->bit_size + graph_pos * bf->bit_size) / CHAR_BIT;
		unsigned char mask = 1 << offsets[i] % CHAR_BIT;
		bf->bits[byte_offset] |= mask;
	}
	bf->nr_entries += nr_entries;
}

static int bloom_filter_check_bits(struct bloom_filter *bf, uint32_t graph_pos, const uint32_t *offsets,
			    int nr)
{
	int i;
	for (i = 0; i < nr; i++) {
		uint32_t byte_offset = (offsets[i] % bf->bit_size + graph_pos * bf->bit_size) / CHAR_BIT;
		unsigned char mask = 1 << offsets[i] % CHAR_BIT;
		if (!(bf->bits[byte_offset] & mask))
			return 0;
	}
	return 1;
}


void bloom_filter_add_hash(struct bloom_filter *bf, uint32_t graph_pos, const unsigned char *hash)
{
	uint32_t offsets[GIT_MAX_RAWSZ / sizeof(uint32_t)];
	hashcpy((unsigned char*)offsets, hash);
	bloom_filter_set_bits(bf, graph_pos, offsets,
			     the_hash_algo->rawsz / sizeof(*offsets), 1);
}

int bloom_filter_check_hash(struct bloom_filter *bf, uint32_t graph_pos, const unsigned char *hash)
{
	uint32_t offsets[GIT_MAX_RAWSZ / sizeof(uint32_t)];
	hashcpy((unsigned char*)offsets, hash);
	return bloom_filter_check_bits(bf, graph_pos, offsets,
			the_hash_algo->rawsz / sizeof(*offsets));
}

void hashxor(const unsigned char *hash1, const unsigned char *hash2,
	     unsigned char *out)
{
	int i;
	for (i = 0; i < the_hash_algo->rawsz; i++)
		out[i] = hash1[i] ^ hash2[i];
}

/* hardcoded for now... */
static GIT_PATH_FUNC(git_path_bloom, "objects/info/bloom")

int bloom_filter_load(struct bloom_filter *bf)
{
	int fd = open(git_path_bloom(), O_RDONLY);

	if (fd < 0)
		return -1;

	read_in_full(fd, &bf->nr_entries, sizeof(bf->nr_entries));
	read_in_full(fd, &bf->commit_nr, sizeof(bf->commit_nr));
	read_in_full(fd, &bf->bit_size, sizeof(bf->bit_size));
	if (bf->bit_size % CHAR_BIT)
		BUG("invalid size for bloom filter");
	bf->bits = xmalloc(bf->commit_nr * bf->bit_size / CHAR_BIT);
	read_in_full(fd, bf->bits, bf->commit_nr * bf->bit_size / CHAR_BIT);

	close(fd);

	return 0;
}

void bloom_filter_write(struct bloom_filter *bf)
{
	int fd = xopen(git_path_bloom(), O_WRONLY | O_CREAT | O_TRUNC, 0666);

	write_in_full(fd, &bf->nr_entries, sizeof(bf->nr_entries));
	write_in_full(fd, &bf->commit_nr, sizeof(bf->commit_nr));
	write_in_full(fd, &bf->bit_size, sizeof(bf->bit_size));
	write_in_full(fd, bf->bits, bf->commit_nr * bf->bit_size / CHAR_BIT);

	close(fd);
}
