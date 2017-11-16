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

struct pack_midx_entry {
	struct object_id oid;
	uint32_t pack_int_id;
	off_t offset;
};

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

#endif
