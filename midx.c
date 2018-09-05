#include "cache.h"
#include "git-compat-util.h"
#include "pack.h"
#include "packfile.h"
#include "midx.h"
#include "object-store.h"

#define MIDX_SIGNATURE 0x4d494458 /* "MIDX" */
#define MIDX_CHUNKID_PACKLOOKUP 0x504c4f4f /* "PLOO" */
#define MIDX_CHUNKID_PACKNAMES 0x504e414d /* "PNAM" */
#define MIDX_CHUNKID_OIDFANOUT 0x4f494446 /* "OIDF" */
#define MIDX_CHUNKID_OIDLOOKUP 0x4f49444c /* "OIDL" */
#define MIDX_CHUNKID_OBJECTOFFSETS 0x4f4f4646 /* "OOFF" */
#define MIDX_CHUNKID_LARGEOFFSETS 0x4c4f4646 /* "LOFF" */

#define MIDX_VERSION_GVFS 0x80000001
#define MIDX_VERSION MIDX_VERSION_GVFS

#define MIDX_OID_VERSION_SHA1 1
#define MIDX_OID_LEN_SHA1 20
#define MIDX_OID_VERSION MIDX_OID_VERSION_SHA1
#define MIDX_OID_LEN MIDX_OID_LEN_SHA1

#define MIDX_LARGE_OFFSET_NEEDED 0x80000000
#define MIDX_CHUNKLOOKUP_WIDTH (sizeof(uint32_t) + sizeof(uint64_t))
#define MIDX_CHUNK_FANOUT_SIZE (sizeof(uint32_t) * 256)
#define MIDX_CHUNK_OFFSET_WIDTH (2 * sizeof(uint32_t))
#define MIDX_CHUNK_LARGE_OFFSET_WIDTH (sizeof(uint64_t))

/* MIDX-git global storage */
struct midxed_git *midxed_git = 0;

struct object_id *get_midx_head_oid(const char *pack_dir, struct object_id *oid)
{
	struct strbuf head_filename = STRBUF_INIT;
	char oid_hex[GIT_MAX_HEXSZ + 1];
	FILE *f;

	strbuf_addstr(&head_filename, pack_dir);
	strbuf_addstr(&head_filename, "/midx-head");

	f = fopen(head_filename.buf, "r");
	strbuf_release(&head_filename);

	if (!f)
		return 0;

	if (!fgets(oid_hex, sizeof(oid_hex), f))
		die("Failed to read midx-head");

	fclose(f);

	if (get_oid_hex(oid_hex, oid))
		return 0;
	return oid;
}

char* get_midx_head_filename_oid(const char *pack_dir,
				 struct object_id *oid)
{
	struct strbuf head_path = STRBUF_INIT;
	strbuf_addstr(&head_path, pack_dir);
	strbuf_addstr(&head_path, "/midx-");
	strbuf_addstr(&head_path, oid_to_hex(oid));
	strbuf_addstr(&head_path, ".midx");

	return strbuf_detach(&head_path, NULL);
}

static char* get_midx_head_filename_dir(const char *pack_dir)
{
	struct object_id oid;
	if (!get_midx_head_oid(pack_dir, &oid))
		return 0;

	return get_midx_head_filename_oid(pack_dir, &oid);
}

struct pack_midx_details_internal {
	uint32_t pack_int_id;
	uint32_t internal_offset;
};

static struct midxed_git *alloc_midxed_git(const char *pack_dir)
{
	struct midxed_git *m = NULL;

	FLEX_ALLOC_MEM(m, pack_dir, pack_dir, strlen(pack_dir));

	return m;
}

static struct midxed_git *load_empty_midxed_git(void)
{
	struct midxed_git *midx = alloc_midxed_git("");

	midx->midx_fd = -1;
	midx->data = NULL;
	midx->num_objects = 0;
	midx->packs = NULL;

	midx->hdr = (void *)midx;
	midx->hdr->num_base_midx = 0;
	midx->hdr->num_packs = 0;
	midx->hdr->num_chunks = 0;

	return 0;
}

static struct midxed_git *load_midxed_git_one(const char *midx_file, const char *pack_dir)
{
	void *midx_map;
	const unsigned char *data;
	struct pack_midx_header *hdr;
	size_t midx_size, packs_len;
	struct stat st;
	uint32_t i;
	struct midxed_git *midx;
	int fd = git_open(midx_file);

	if (fd < 0)
		return 0;
	if (fstat(fd, &st)) {
		close(fd);
		return 0;
	}
	midx_size = xsize_t(st.st_size);

	if (midx_size < 16 + 8 * 5 + 4 * 256 + GIT_MAX_RAWSZ) {
		close(fd);
		die("midx file %s is too small", midx_file);
	}
	midx_map = xmmap(NULL, midx_size, PROT_READ, MAP_PRIVATE, fd, 0);
	data = (const unsigned char *)midx_map;

	hdr = midx_map;
	if (ntohl(hdr->midx_signature) != MIDX_SIGNATURE) {
		uint32_t signature = ntohl(hdr->midx_signature);
		munmap(midx_map, midx_size);
		close(fd);
		die("midx signature %X does not match signature %X",
		    signature, MIDX_SIGNATURE);
	}

	if (ntohl(hdr->midx_version) != MIDX_VERSION) {
		uint32_t version = ntohl(hdr->midx_version);
		munmap(midx_map, midx_size);
		close(fd);
		die("midx version %X does not match version %X",
		    version, MIDX_VERSION);
	}

	/* Time to fill a midx struct */
	midx = alloc_midxed_git(pack_dir);

	midx->hdr = hdr;
	midx->midx_fd = fd;
	midx->data = midx_map;
	midx->data_len = midx_size;

	/* read chunk ids to find pointers */
	for (i = 0; i <= hdr->num_chunks; i++) {
		uint32_t chunk_id = ntohl(*(uint32_t*)(data + sizeof(*hdr) + 12 * i));
		uint64_t chunk_offset1 = ntohl(*(uint32_t*)(data + sizeof(*hdr) + 12 * i + 4));
		uint32_t chunk_offset2 = ntohl(*(uint32_t*)(data + sizeof(*hdr) + 12 * i + 8));
		uint64_t chunk_offset = (chunk_offset1 << 32) | chunk_offset2;

		if (sizeof(data) == 4 && chunk_offset >> 32) {
			munmap(midx_map, midx_size);
			close(fd);
			die(_("unable to memory-map in 32-bit address space"));
		}

		switch (chunk_id) {
			case MIDX_CHUNKID_PACKLOOKUP:
				midx->chunk_pack_lookup = data + chunk_offset;
				break;

			case MIDX_CHUNKID_PACKNAMES:
				midx->chunk_pack_names = data + chunk_offset;
				break;

			case MIDX_CHUNKID_OIDFANOUT:
				midx->chunk_oid_fanout = data + chunk_offset;
				break;

			case MIDX_CHUNKID_OIDLOOKUP:
				midx->chunk_oid_lookup = data + chunk_offset;
				break;

			case MIDX_CHUNKID_OBJECTOFFSETS:
				midx->chunk_object_offsets = data + chunk_offset;
				break;

			case MIDX_CHUNKID_LARGEOFFSETS:
				midx->chunk_large_offsets = data + chunk_offset;
				break;

			default:
				/* We allow optional MIDX chunks, so ignore unrecognized chunk ids */
				break;
		}
	}

	if (!midx->chunk_oid_fanout)
		die("midx missing OID Fanout chunk");
	if (!midx->chunk_pack_lookup)
		die("midx missing Packfile Name Lookup chunk");
	if (!midx->chunk_pack_names)
		die("midx missing Packfile Name chunk");

	midx->num_objects = ntohl(*((uint32_t*)(midx->chunk_oid_fanout + 255 * 4)));
	midx->num_packs = ntohl(midx->hdr->num_packs);

	packs_len = st_mult(sizeof(struct packed_git*), midx->num_packs);

	if (packs_len) {
		ALLOC_ARRAY(midx->packs, midx->num_packs);
		ALLOC_ARRAY(midx->pack_names, midx->num_packs);
		memset(midx->packs, 0, packs_len);

		for (i = 0; i < midx->num_packs; i++) {
			uint32_t name_offset = ntohl(*(uint32_t*)(midx->chunk_pack_lookup + 4 * i));

			if (midx->chunk_pack_names + name_offset >= midx->data + midx->data_len)
				die("invalid packfile name lookup");

			midx->pack_names[i] = (const char*)(midx->chunk_pack_names + name_offset);
		}
	}

	return midx;
}

struct midxed_git *get_midxed_git(const char *pack_dir, struct object_id *oid)
{
	struct midxed_git *m;
	char *fname = get_midx_head_filename_oid(pack_dir, oid);
	m = load_midxed_git_one(fname, pack_dir);
	free(fname);
	return m;
}

static int prepare_midxed_git_head(char *pack_dir, int local)
{
	struct midxed_git *m = midxed_git;
	struct midxed_git *m_search;
	char *midx_head_path;

	if (!core_midx)
		return 1;

	for (m_search = midxed_git; m_search; m_search = m_search->next) {
		if (!strcmp(pack_dir, m_search->pack_dir))
			return 1;
	}

	midx_head_path = get_midx_head_filename_dir(pack_dir);
	if (midx_head_path) {
		midxed_git = load_midxed_git_one(midx_head_path, pack_dir);
		midxed_git->next = m;
		free(midx_head_path);
	} else if (!m) {
		midxed_git = load_empty_midxed_git();
	}

	return !midxed_git;
}

int prepare_midxed_git_objdir(char *obj_dir, int local)
{
	int ret;
	struct strbuf pack_dir = STRBUF_INIT;
	strbuf_addstr(&pack_dir, obj_dir);
	strbuf_add(&pack_dir, "/pack", 5);

	ret = prepare_midxed_git_head(pack_dir.buf, local);
	strbuf_release(&pack_dir);
	return ret;
}

struct pack_midx_details *nth_midxed_object_details(struct midxed_git *m,
						    uint32_t n,
						    struct pack_midx_details *d)
{
	struct pack_midx_details_internal *d_internal;
	const unsigned char *details = m->chunk_object_offsets;

	if (n >= m->num_objects) {
		return NULL;
	}

	d_internal = (struct pack_midx_details_internal*)(details + 8 * n);
	d->pack_int_id = ntohl(d_internal->pack_int_id);
	d->offset = ntohl(d_internal->internal_offset);

	if (m->chunk_large_offsets && d->offset & MIDX_LARGE_OFFSET_NEEDED) {
		uint32_t large_offset = d->offset ^ MIDX_LARGE_OFFSET_NEEDED;
		const unsigned char *large_offsets = m->chunk_large_offsets + 8 * large_offset;

		d->offset =  (((uint64_t)ntohl(*((uint32_t *)(large_offsets + 0)))) << 32) |
					 ntohl(*((uint32_t *)(large_offsets + 4)));
	}

	return d;
}

struct pack_midx_entry *nth_midxed_object_entry(struct midxed_git *m,
						uint32_t n,
						struct pack_midx_entry *e)
{
	struct pack_midx_details details;
	const unsigned char *index = m->chunk_oid_lookup;

	if (!nth_midxed_object_details(m, n, &details))
		return NULL;

	memcpy(e->oid.hash, index + m->hdr->hash_len * n, m->hdr->hash_len);
	e->pack_int_id = details.pack_int_id;
	e->offset = details.offset;

	/* Use zero for mtime so this entry is "older" than new duplicates */
	e->pack_mtime = 0;

	return e;
}

const struct object_id *nth_midxed_object_oid(struct object_id *oid,
					      struct midxed_git *m,
					      uint32_t n)
{
	struct pack_midx_entry e;

	if (!nth_midxed_object_entry(m, n, &e))
		return 0;

	hashcpy(oid->hash, e.oid.hash);
	return oid;
}

int bsearch_midx(struct midxed_git *m, const unsigned char *sha1, uint32_t *pos)
{
	uint32_t last, first = 0;

	if (sha1[0])
		first = ntohl(*(uint32_t*)(m->chunk_oid_fanout + 4 * (sha1[0] - 1)));
	last = ntohl(*(uint32_t*)(m->chunk_oid_fanout + 4 * sha1[0]));

	while (first < last) {
		uint32_t mid = first + (last - first) / 2;
		const unsigned char *current;
		int cmp;

		current = m->chunk_oid_lookup + m->hdr->hash_len * mid;
		cmp = hashcmp(sha1, current);
		if (!cmp) {
			*pos = mid;
			return 1;
		}
		if (cmp > 0) {
			first = mid + 1;
			continue;
		}
		last = mid;
	}

	*pos = first;
	return 0;
}

static int prepare_midx_pack(struct midxed_git *m, uint32_t pack_int_id)
{
	struct strbuf pack_name = STRBUF_INIT;

	if (pack_int_id >= m->hdr->num_packs)
		return 1;

	if (m->packs[pack_int_id])
		return 0;

	strbuf_addstr(&pack_name, m->pack_dir);
	strbuf_addstr(&pack_name, "/");
	strbuf_addstr(&pack_name, m->pack_names[pack_int_id]);
	strbuf_strip_suffix(&pack_name, ".pack");
	strbuf_addstr(&pack_name, ".idx");

	m->packs[pack_int_id] = add_packed_git(pack_name.buf, pack_name.len, 1);
	strbuf_release(&pack_name);
	return !m->packs[pack_int_id];
}

static int find_pack_entry_midx(const struct object_id *oid,
				struct midxed_git *m,
				struct packed_git **p,
				off_t *offset)
{
	uint32_t pos;
	struct pack_midx_details d;

	if (!bsearch_midx(m, oid->hash, &pos) ||
	    !nth_midxed_object_details(m, pos, &d))
		return 0;

	if (d.pack_int_id >= m->num_packs)
		die(_("Bad pack-int-id"));

	/* load packfile, if necessary */
	if (prepare_midx_pack(m, d.pack_int_id))
		return 0;

	*p = m->packs[d.pack_int_id];
	*offset = d.offset;

	return 1;
}

int fill_pack_entry_midx(const struct object_id *oid,
			 struct pack_entry *e)
{
	struct packed_git *p;
	struct midxed_git *m;

	if (!core_midx)
		return 0;

	m = midxed_git;
	while (m)
	{
		off_t offset;
		if (!find_pack_entry_midx(oid, m, &p, &offset)) {
			m = m->next;
			continue;
		}

		/*
		* We are about to tell the caller where they can locate the
		* requested object.  We better make sure the packfile is
		* still here and can be accessed before supplying that
		* answer, as it may have been deleted since the MIDX was
		* loaded!
		*/
		if (!is_pack_valid(p))
			return 0;

		e->offset = offset;
		e->p = p;

		return 1;
	}

	return 0;
}

int contains_pack(struct midxed_git *m, const char *pack_name)
{
	uint32_t first = 0, last = m->num_packs;

	while (first < last) {
		uint32_t mid = first + (last - first) / 2;
		const char *current;
		int cmp;

		current = m->pack_names[mid];
		cmp = strcmp(pack_name, current);
		if (!cmp)
			return 1;
		if (cmp > 0) {
			first = mid + 1;
			continue;
		}
		last = mid;
	}

	return 0;
}

static size_t write_midx_chunk_packlookup(
	struct hashfile *f,
	const char **pack_names, uint32_t nr_packs)
{
	uint32_t i, cur_len = 0;

	for (i = 0; i < nr_packs; i++) {
		hashwrite_be32(f, cur_len);
		cur_len += strlen(pack_names[i]) + 1;
	}

	return sizeof(uint32_t) * (size_t)nr_packs;
}

static size_t write_midx_chunk_packnames(
	struct hashfile *f,
	const char **pack_names, uint32_t nr_packs)
{
	uint32_t i;
	size_t written = 0;
	for (i = 0; i < nr_packs; i++) {
		size_t writelen = strlen(pack_names[i]) + 1;
		if (i > 0 && strcmp(pack_names[i], pack_names[i-1]) <= 0)
			BUG("incorrect pack order: %s before %s",
			    pack_names[i-1],
			    pack_names[i]);

		hashwrite(f, pack_names[i], writelen);
		written += writelen;
	}

	return written;
}

static size_t write_midx_chunk_oidfanout(
	struct hashfile *f,
	struct pack_midx_entry *objects, uint32_t nr_objects)
{
	struct pack_midx_entry *list = objects;
	struct pack_midx_entry *last = objects + nr_objects;
	uint32_t count_distinct = 0;
	uint32_t i;

	/*
	* Write the first-level table (the list is sorted,
	* but we use a 256-entry lookup to be able to avoid
	* having to do eight extra binary search iterations).
	*/
	for (i = 0; i < 256; i++) {
		struct pack_midx_entry *next = list;
		struct pack_midx_entry *prev = NULL;

		while (next < last) {
			if (next->oid.hash[0] != i)
				break;

			if (!prev || oidcmp(&(prev->oid), &(next->oid)))
				count_distinct++;

			prev = next++;
		}

		hashwrite_be32(f, count_distinct);
		list = next;
	}

	return MIDX_CHUNK_FANOUT_SIZE;
}

static size_t write_midx_chunk_oidlookup(
	struct hashfile *f, unsigned char hash_len,
	struct pack_midx_entry *objects, uint32_t nr_objects)
{
	struct pack_midx_entry *list = objects;
	struct object_id *last_oid = NULL;
	uint32_t i;
	size_t written = 0;

	for (i = 0; i < nr_objects; i++) {
		struct pack_midx_entry *obj = list++;

		if (i < nr_objects - 1) {
			/* Check out-of-order */
			struct pack_midx_entry *next = list;
			if (oidcmp(&obj->oid, &next->oid) >= 0)
				BUG("OIDs not in order: %s >= %s",
				oid_to_hex(&obj->oid),
				oid_to_hex(&next->oid));
		}

		/* Skip duplicate objects */
		if (last_oid && !oidcmp(last_oid, &obj->oid))
			continue;

		last_oid = &obj->oid;
		hashwrite(f, obj->oid.hash, (int)hash_len);
		written += hash_len;
	}

	return written;
}

static size_t write_midx_chunk_objectoffsets(
	struct hashfile *f, int large_offset_needed,
	struct pack_midx_entry *objects, uint32_t nr_objects, uint32_t *pack_perm)
{
	struct pack_midx_entry *list = objects;
	struct object_id *last_oid = 0;
	uint32_t i, nr_large_offset = 0;
	size_t written = 0;

	for (i = 0; i < nr_objects; i++) {
		struct pack_midx_entry *obj = list++;

		if (last_oid && !oidcmp(last_oid, &obj->oid))
			continue;

		last_oid = &obj->oid;

		hashwrite_be32(f, pack_perm[obj->pack_int_id]);

		if (large_offset_needed && obj->offset >> 31)
			hashwrite_be32(f, MIDX_LARGE_OFFSET_NEEDED | nr_large_offset++);
		else if (!large_offset_needed && obj->offset >> 32)
			BUG("object %s requires a large offset (%"PRIx64") but the MIDX is not writing large offsets!",
			    oid_to_hex(&obj->oid),
			    obj->offset);
		else
			hashwrite_be32(f, (uint32_t)obj->offset);

		written += 2 * sizeof(uint32_t);
	}

	return written;
}

static size_t write_midx_chunk_largeoffsets(
	struct hashfile *f, uint32_t nr_large_offset,
	struct pack_midx_entry *objects, uint32_t nr_objects)
{
	struct pack_midx_entry *list = objects;
	struct object_id *last_oid = 0;
	size_t written = 0;

	while (nr_large_offset) {
		struct pack_midx_entry *obj = list++;
		uint64_t offset = obj->offset;

		if (last_oid && !oidcmp(last_oid, &obj->oid))
			continue;

		last_oid = &obj->oid;

		if (!(offset >> 31))
			continue;

		hashwrite_be32(f, offset >> 32);
		hashwrite_be32(f, offset & 0xffffffff);
		written += 2 * sizeof(uint32_t);

		nr_large_offset--;
	}

	return written;
}

struct pack_pair {
	uint32_t pack_int_id;
	const char *pack_name;
};

static int pack_pair_compare(const void *_a, const void *_b)
{
	struct pack_pair *a = (struct pack_pair *)_a;
	struct pack_pair *b = (struct pack_pair *)_b;
	return strcmp(a->pack_name, b->pack_name);
}

static void sort_packs_by_name(const char **pack_names, uint32_t nr_packs, uint32_t *perm)
{
	uint32_t i;
	struct pack_pair *pairs;

	ALLOC_ARRAY(pairs, nr_packs);

	for (i = 0; i < nr_packs; i++) {
		pairs[i].pack_int_id = i;
		pairs[i].pack_name = pack_names[i];
	}

	QSORT(pairs, nr_packs, pack_pair_compare);

	for (i = 0; i < nr_packs; i++) {
		pack_names[i] = pairs[i].pack_name;
		perm[pairs[i].pack_int_id] = i;
	}
}

const char *write_midx_file(const char *pack_dir,
			    const char *midx_name,
			    const char **pack_names,
			    uint32_t nr_packs,
			    struct pack_midx_entry *objects,
			    uint32_t nr_objects)
{
	struct hashfile *f;
	int i, chunk, fd;
	struct pack_midx_header hdr;
	uint32_t chunk_ids[7];
	uint64_t chunk_offsets[7];
	unsigned char large_offset_needed = 0;
	unsigned int nr_large_offset = 0;
	unsigned char final_hash[GIT_MAX_RAWSZ];
	const char *final_hex;
	int rename_needed = 0;
	int total_name_len = 0;
	uint32_t *pack_perm;
	size_t written = 0;

	if (!core_midx)
		return 0;

	/* determine if large offsets are required */
	for (i = 0; i < nr_objects; i++) {
		if (objects[i].offset > 0x7fffffff)
			nr_large_offset++;
		if (objects[i].offset > 0xffffffff)
			large_offset_needed = 1;
	}

	/* Sort packs */
	if (nr_packs) {
		ALLOC_ARRAY(pack_perm, nr_packs);
		sort_packs_by_name(pack_names, nr_packs, pack_perm);
	} else {
		pack_perm = 0;
	}

	if (nr_packs) {
		for (i = 0; i < nr_packs; i++) {
			total_name_len += strlen(pack_names[i]) + 1;
		}
	}

	/* open temp file, or direct file if given */
	if (!midx_name) {
		struct strbuf tmp_file = STRBUF_INIT;
		strbuf_addstr(&tmp_file, pack_dir);
		strbuf_addstr(&tmp_file, "/tmp_midx_XXXXXX");

		fd = git_mkstemp_mode(tmp_file.buf, 0444);
		if (fd < 0)
			die_errno("unable to create '%s'", tmp_file.buf);

		midx_name = strbuf_detach(&tmp_file, NULL);
		rename_needed = 1;
	} else {
		unlink(midx_name);
		fd = open(midx_name, O_CREAT|O_EXCL|O_WRONLY, 0600);

		if (fd < 0)
			die_errno("unable to create '%s'", midx_name);
	}
	f = hashfd(fd, midx_name);

	/* fill header info */
	hdr.midx_signature = htonl(MIDX_SIGNATURE);
	hdr.midx_version = htonl(MIDX_VERSION);

	hdr.hash_version = MIDX_OID_VERSION;
	hdr.hash_len = MIDX_OID_LEN;
	hdr.num_base_midx = 0;
	hdr.num_packs = htonl(nr_packs);

	/*
	 * We expect the following chunks, which are required:
	 *
	 * Packfile Name Lookup
	 * Packfile Names
	 * OID Fanout
	 * OID Lookup
	 * Object Offsets
	 */
	hdr.num_chunks = large_offset_needed ? 6 : 5;

	/* write header to file */
	assert(sizeof(hdr) == 16);
	hashwrite(f, &hdr, sizeof(hdr));
	written += sizeof(hdr);

	/*
	 * Fill initial chunk values using offsets
	 * relative to first chunk.
	 */
	chunk_offsets[0] = sizeof(hdr) + MIDX_CHUNKLOOKUP_WIDTH * (hdr.num_chunks + 1);
	chunk_ids[0] = MIDX_CHUNKID_PACKLOOKUP;
	chunk_offsets[1] = chunk_offsets[0] + nr_packs * 4;
	chunk_ids[1] = MIDX_CHUNKID_OIDFANOUT;
	chunk_offsets[2] = chunk_offsets[1] + MIDX_CHUNK_FANOUT_SIZE;
	chunk_ids[2] = MIDX_CHUNKID_OIDLOOKUP;
	chunk_offsets[3] = chunk_offsets[2] + (uint64_t)nr_objects
					    * (uint64_t)hdr.hash_len;
	chunk_ids[3] = MIDX_CHUNKID_OBJECTOFFSETS;
	chunk_offsets[4] = chunk_offsets[3] + MIDX_CHUNK_OFFSET_WIDTH * (uint64_t)nr_objects;

	if (large_offset_needed) {
		chunk_ids[4] = MIDX_CHUNKID_LARGEOFFSETS;
		chunk_offsets[5] = chunk_offsets[4] + MIDX_CHUNK_LARGE_OFFSET_WIDTH * (uint64_t)nr_large_offset;
		chunk_ids[5] = MIDX_CHUNKID_PACKNAMES;
		chunk_offsets[6] = chunk_offsets[5] + total_name_len;
		chunk_ids[6] = 0;
	} else {
		chunk_ids[4] = MIDX_CHUNKID_PACKNAMES;
		chunk_offsets[5] = chunk_offsets[4] + total_name_len;
		chunk_ids[5] = 0;
	}

	for (i = 0; i <= hdr.num_chunks; i++) {
		hashwrite_be32(f, chunk_ids[i]);
		hashwrite_be32(f, chunk_offsets[i] >> 32);
		hashwrite_be32(f, chunk_offsets[i] & 0xffffffff);
		written += MIDX_CHUNKLOOKUP_WIDTH;
	}

	for (chunk = 0; chunk <= hdr.num_chunks; chunk++) {
		if (chunk_offsets[chunk] != written)
			BUG("chunk %d has intended chunk offset %"PRIx64" does not match expected %"PRIx64"",
			    chunk,
			    (uint64_t)chunk_offsets[chunk],
			    (uint64_t)written);

		switch (chunk_ids[chunk]) {
		case MIDX_CHUNKID_PACKLOOKUP:
			written += write_midx_chunk_packlookup(f, pack_names, nr_packs);
			break;

		case MIDX_CHUNKID_PACKNAMES:
			written += write_midx_chunk_packnames(f, pack_names, nr_packs);
			break;

		case MIDX_CHUNKID_OIDFANOUT:
			written += write_midx_chunk_oidfanout(f, objects, nr_objects);
			break;

		case MIDX_CHUNKID_OIDLOOKUP:
			written += write_midx_chunk_oidlookup(f, hdr.hash_len, objects,
							      nr_objects);
			break;

		case MIDX_CHUNKID_OBJECTOFFSETS:
			written += write_midx_chunk_objectoffsets(f, large_offset_needed,
								  objects, nr_objects,
								  pack_perm);
			break;

		case MIDX_CHUNKID_LARGEOFFSETS:
			written += write_midx_chunk_largeoffsets(f, nr_large_offset,
								 objects, nr_objects);
			break;

		case 0:
			break;

		default:
			BUG("midx tried to write an invalid chunk ID %08X", chunk_ids[chunk]);
			break;
		}
	}

	finalize_hashfile(f, final_hash, CSUM_CLOSE | CSUM_FSYNC | CSUM_HASH_IN_STREAM);

	if (rename_needed)
	{
		struct object_id oid;
		char *fname;

		memcpy(oid.hash, final_hash, GIT_MAX_RAWSZ);
		fname = get_midx_head_filename_oid(pack_dir, &oid);
		final_hex = sha1_to_hex(final_hash);

		if (rename(midx_name, fname))
			die("failed to rename %s to %s", midx_name, fname);

		free(fname);
	} else {
		final_hex = midx_name;
	}

	return final_hex;
}

int close_midx(struct midxed_git *m)
{
	int i;
	if (m->midx_fd < 0)
		return 0;

	for (i = 0; i < m->num_packs; i++) {
		if (m->packs[i]) {
			close_pack(m->packs[i]);
			free(m->packs[i]);
			m->packs[i] = NULL;
		}
	}

	munmap((void *)m->data, m->data_len);
	m->data = 0;

	close(m->midx_fd);
	m->midx_fd = -1;

	free(m->packs);
	free(m->pack_names);

	return 1;
}

void close_all_midx(void)
{
	struct midxed_git *m = midxed_git;
	struct midxed_git *next;

	while (m) {
		next = m->next;
		close_midx(m);
		free(m);
		m = next;
	}

	midxed_git = 0;
}

static int verify_midx_error = 0;

static void midx_report(const char *fmt, ...)
{
	va_list ap;
	struct strbuf sb = STRBUF_INIT;
	verify_midx_error = 1;

	va_start(ap, fmt);
	strbuf_vaddf(&sb, fmt, ap);

	fprintf(stderr, "%s\n", sb.buf);
	strbuf_release(&sb);
	va_end(ap);
}

int midx_verify(const char *pack_dir, const char *midx_id)
{
	uint32_t i, cur_fanout_pos = 0;
	struct midxed_git *m;
	const char *midx_head_path;
	struct object_id cur_oid, prev_oid, checksum;
	struct hashfile *f;
	int devnull, checksum_fail = 0;

	if (midx_id) {
		size_t sz;
		struct strbuf sb = STRBUF_INIT;
		strbuf_addf(&sb, "%s/midx-%s.midx", pack_dir, midx_id);
		midx_head_path = strbuf_detach(&sb, &sz);
	} else {
		midx_head_path = get_midx_head_filename_dir(pack_dir);
	}

	m = load_midxed_git_one(midx_head_path, pack_dir);

	if (!m) {
		midx_report("failed to find specified midx file");
		goto cleanup;
	}


	devnull = open("/dev/null", O_WRONLY);
	f = hashfd(devnull, NULL);
	hashwrite(f, m->data, m->data_len - m->hdr->hash_len);
	finalize_hashfile(f, checksum.hash, CSUM_CLOSE);
	if (hashcmp(checksum.hash, m->data + m->data_len - m->hdr->hash_len)) {
		midx_report(_("the midx file has incorrect checksum and is likely corrupt"));
		verify_midx_error = 0;
		checksum_fail = 1;
	}

	if (m->hdr->hash_version != MIDX_OID_VERSION)
		midx_report("invalid hash version");
	if (m->hdr->hash_len != MIDX_OID_LEN)
		midx_report("invalid hash length");

	if (verify_midx_error)
		goto cleanup;

	if (!m->chunk_oid_lookup)
		midx_report("missing OID Lookup chunk");
	if (!m->chunk_object_offsets)
		midx_report("missing Object Offset chunk");

	if (verify_midx_error)
		goto cleanup;

	for (i = 0; i < m->num_packs; i++) {
		if (prepare_midx_pack(m, i)) {
			midx_report("failed to prepare pack %s",
				    m->pack_names[i]);
			continue;
		}

		if (!m->packs[i]->index_data &&
		    open_pack_index(m->packs[i]))
			midx_report("failed to open index for pack %s",
				    m->pack_names[i]);
	}

	if (verify_midx_error)
		goto cleanup;

	for (i = 0; i < m->num_objects; i++) {
		struct pack_midx_details details;
		uint32_t index_pos, pack_id;
		struct packed_git *p;
		off_t pack_offset;

		hashcpy(cur_oid.hash, m->chunk_oid_lookup + m->hdr->hash_len * i);

		while (cur_oid.hash[0] > cur_fanout_pos) {
			uint32_t fanout_value = get_be32(m->chunk_oid_fanout + cur_fanout_pos * sizeof(uint32_t));
			if (i != fanout_value)
				midx_report("midx has incorrect fanout value: fanout[%d] = %u != %u",
					    cur_fanout_pos, fanout_value, i);

			cur_fanout_pos++;
		}

		if (i && oidcmp(&prev_oid, &cur_oid) >= 0)
			midx_report("midx has incorrect OID order: %s then %s",
				    oid_to_hex(&prev_oid),
				    oid_to_hex(&cur_oid));

		oidcpy(&prev_oid, &cur_oid);

		if (!nth_midxed_object_details(m, i, &details)) {
			midx_report("nth_midxed_object_details failed with n=%d", i);
			continue;
		}

		pack_id = details.pack_int_id;
		if (pack_id >= m->num_packs) {
			midx_report("pack-int-id for object n=%d is invalid: %u",
				    pack_id);
			continue;
		}

		p = m->packs[pack_id];

		if (!find_pack_entry_pos(cur_oid.hash, p, &index_pos)) {
			midx_report("midx contains object not present in packfile: %s",
				    oid_to_hex(&cur_oid));
			continue;
		}

		pack_offset = nth_packed_object_offset(p, index_pos);
		if (details.offset != pack_offset)
			midx_report("midx has incorrect offset for %s : %"PRIx64" != %"PRIx64,
				    oid_to_hex(&cur_oid),
				    details.offset,
				    pack_offset);
	}

cleanup:
	if (m)
		close_midx(m);
	free(m);
	return verify_midx_error | checksum_fail;
}
