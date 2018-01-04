#ifndef MIDX_H
#define MIDX_H

#include "git-compat-util.h"
#include "object.h"
#include "csum-file.h"

extern char *get_midx_filename_oid(const char *pack_dir,
				   struct object_id *oid);

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

/*
 * Write a single MIDX file storing the given entries for the
 * given list of packfiles. If midx_name is null, then a temp
 * file will be created and swapped using the result hash value.
 * Otherwise, write directly to midx_name.
 *
 * Returns the final name of the MIDX file within pack_dir.
 */
extern const char *write_midx_file(const char *pack_dir,
				   const char *midx_name,
				   const char **pack_names,
				   uint32_t nr_packs,
				   struct pack_midx_entry **objects,
				   uint32_t nr_objects);

#endif
