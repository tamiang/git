#ifndef MIDX_H
#define MIDX_H

#include "git-compat-util.h"
#include "object.h"
#include "csum-file.h"

#define MIDX_SIGNATURE 0x4d494458 /* "MIDX" */
#define MIDX_CHUNKID_PACKLOOKUP 0x504c4f4f /* "PLOO" */
#define MIDX_CHUNKID_PACKNAMES 0x504e414d /* "PNAM" */
#define MIDX_CHUNKID_OIDFANOUT 0x4f494446 /* "OIDF" */
#define MIDX_CHUNKID_OIDLOOKUP 0x4f49444c /* "OIDL" */
#define MIDX_CHUNKID_OBJECTOFFSETS 0x4f4f4646 /* "OOFF" */
#define MIDX_CHUNKID_LARGEOFFSETS 0x4c4f4646 /* "LOFF" */

#define MIDX_VERSION_GVFS 0x80000001
#define MIDX_VERSION MIDX_VERSION_GVFS

#define MIDX_HASH_VERSION_SHA1 1
#define MIDX_HASH_LEN_SHA1 20
#define MIDX_HASH_VERSION MIDX_HASH_VERSION_SHA1
#define MIDX_HASH_LEN MIDX_HASH_LEN_SHA1

extern struct object_id *get_midx_head_oid(const char *pack_dir, struct object_id *oid);

struct pack_midx_entry {
	struct object_id oid;
	uint32_t pack_int_id;
	off_t offset;
};

struct pack_midx_header {
	uint32_t midx_signature;
	uint32_t midx_version;
	unsigned char hash_version;
	unsigned char hash_len;
	unsigned char num_base_midx;
	unsigned char num_chunks;
	uint32_t num_packs;
};

struct midxed_git {
	struct midxed_git *next;

	int midx_fd;

	/* the mmap'd data for the midx file */
	const unsigned char *data;
	size_t data_len;

	/* points into the mmap'd data */
	struct pack_midx_header *hdr;

	/* can construct filename from obj_dir + "/packs/midx-" + oid + ".midx" */
	struct object_id oid;

	/* derived from the fanout chunk */
	uint32_t num_objects;

	/* converted number of packs */
	uint32_t num_packs;

	/* hdr->num_packs * 4 bytes */
	const unsigned char *chunk_pack_lookup;
	const unsigned char *chunk_pack_names;

	/* 256 * 4 bytes */
	const unsigned char *chunk_oid_fanout;

	/* num_objects * hdr->hash_len bytes */
	const unsigned char *chunk_oid_lookup;

	/* num_objects * 8 bytes */
	const unsigned char *chunk_object_offsets;

	/*
	 * 8 bytes per large offset.
	 * (Optional: may be null.)
	 */
	const unsigned char *chunk_large_offsets;

	/*
	 * Points into mmap'd data storing the pack filenames.
	 */
	const char **pack_names;

	/*
	 * Store an array of pack-pointers. If NULL, then the
	 * pack has not been loaded yet. The array indices
	 * correspond to the pack_int_ids from the midx storage.
	 */
	struct packed_git **packs;

	/* something like ".git/objects/pack" */
	char pack_dir[FLEX_ARRAY]; /* more */
};

extern struct midxed_git *get_midxed_git(const char *pack_dir, struct object_id *midx_oid);

struct pack_midx_details {
	uint32_t pack_int_id;
	off_t offset;
};

extern struct pack_midx_details *nth_midxed_object_details(struct midxed_git *m,
							   uint32_t n,
							   struct pack_midx_details *d);
extern struct pack_midx_entry *nth_midxed_object_entry(struct midxed_git *m,
						       uint32_t n,
						       struct pack_midx_entry *e);

extern int contains_pack(struct midxed_git *m, const char *pack_name);

/*
 * Write a single MIDX file storing the given entries for the
 * given list of packfiles. If midx_name is null, then a temp
 * file will be created and swapped using the result hash value.
 * Otherwise, write directly to midx_name.
 *
 * Returns the final name of the MIDX file within pack_dir.
 */
extern const char *write_midx_file(
	const char *pack_dir,
	const char *midx_name,
	const char **pack_names,          uint32_t nr_packs,
	struct pack_midx_entry **objects, uint32_t nr_objects);

extern int close_midx(struct midxed_git *m);

#endif
