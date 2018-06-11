#include "builtin.h"
#include "cache.h"
#include "config.h"
#include "dir.h"
#include "git-compat-util.h"
#include "lockfile.h"
#include "packfile.h"
#include "parse-options.h"
#include "midx.h"
#include "object-store.h"

static char const * const builtin_midx_usage[] ={
	N_("git midx [--pack-dir <packdir>]"),
	N_("git midx --write [--pack-dir <packdir>] [--update-head] [--delete-expired]"),
	N_("git midx --read [--midx-id=<oid>]"),
	N_("git midx --clear [--pack-dir <packdir>]"),
	N_("git midx --verify [--pack-dir <packdir>]"),
	NULL
};

static struct opts_midx {
	const char *pack_dir;
	int write;
	int update_head;
	int delete_expired;
	int read;
	const char *midx_id;
	int clear;
	int verify;
	int has_existing;
	struct object_id old_midx_oid;
} opts;

static int midx_oid_compare(const void *_a, const void *_b)
{
	struct pack_midx_entry *a = (struct pack_midx_entry *)_a;
	struct pack_midx_entry *b = (struct pack_midx_entry *)_b;
	int cmp = oidcmp(&a->oid, &b->oid);

	if (cmp)
		return cmp;

	if (a->pack_mtime > b->pack_mtime)
		return -1;
	else if (a->pack_mtime < b->pack_mtime)
		return 1;

	return a->pack_int_id - b->pack_int_id;
}

static uint32_t get_pack_fanout(struct packed_git *p, uint32_t value)
{
	const uint32_t *level1_ofs = p->index_data;

	if (!level1_ofs) {
		if (open_pack_index(p))
			return 0;
		level1_ofs = p->index_data;
	}

	if (p->index_version > 1) {
		level1_ofs += 2;
	}

	return ntohl(level1_ofs[value]);
}

/*
 * It is possible to artificially get into a state where there are many
 * duplicate copies of objects. That can create high memory pressure if
 * we are to create a list of all objects before de-duplication. To reduce
 * this memory pressure without a significant performance drop, automatically
 * group objects by the first byte of their object id. Use the IDX fanout
 * tables to group the data, copy to a local array, then sort.
 *
 * Copy only the de-duplicated entries (selected by most-recent modified time
 * of a packfile containing the object).
 */
static void dedupe_and_sort_entries(
	struct packed_git **packs, uint32_t nr_packs,
	struct midxed_git *midx,
	struct pack_midx_entry **objects, uint32_t *nr_objects)
{
	uint32_t first_byte, i;
	struct pack_midx_entry *objects_batch = NULL;
	uint32_t nr_objects_batch = 0;
	uint32_t alloc_objects_batch = 0;
	uint32_t alloc_objects;
	uint32_t pack_offset = 0;
	struct pack_midx_entry *local_objects = NULL;
	int nr_local_objects = 0;

	if (midx) {
		nr_objects_batch = midx->num_objects;
		pack_offset = midx->num_packs;
	}

	for (i = pack_offset; i < nr_packs; i++)
		nr_objects_batch += packs[i]->num_objects;

	/*
	 * Predict the size of the batches to be roughly ~1/256 the total
	 * count, but give some slack as they will not be equally sized.
	 */
	alloc_objects_batch = nr_objects_batch / 200;
	ALLOC_ARRAY(objects_batch, alloc_objects_batch);

	*nr_objects = 0;
	alloc_objects = alloc_objects_batch;
	ALLOC_ARRAY(local_objects, alloc_objects);

	for (first_byte = 0; first_byte < 256; first_byte++) {
		nr_objects_batch = 0;

		if (midx) {
			uint32_t start, end;
			if (first_byte)
				start = get_be32(midx->chunk_oid_fanout + 4 * (first_byte - 1));
			else
				start = 0;

			end = get_be32(midx->chunk_oid_fanout + 4 * first_byte);

			while (start < end) {
				ALLOC_GROW(objects_batch, nr_objects_batch + 1, alloc_objects_batch);
				nth_midxed_object_entry(midx, start, &objects_batch[nr_objects_batch]);
				nr_objects_batch++;
				start++;
			}
		}

		for (i = pack_offset; i < nr_packs; i++) {
			uint32_t start, end;

			if (first_byte)
				start = get_pack_fanout(packs[i], first_byte - 1);
			else
				start = 0;
			end = get_pack_fanout(packs[i], first_byte);

			while (start < end) {
				struct pack_midx_entry *entry;
				ALLOC_GROW(objects_batch, nr_objects_batch + 1, alloc_objects_batch);
				entry = &objects_batch[nr_objects_batch++];

				if (!nth_packed_object_oid(&entry->oid, packs[i], start))
					die("unable to get sha1 of object %u in %s",
					start, packs[i]->pack_name);

				entry->pack_int_id = i;
				entry->offset = nth_packed_object_offset(packs[i], start);
				entry->pack_mtime = packs[i]->mtime;
				start++;
			}
		}

		QSORT(objects_batch, nr_objects_batch, midx_oid_compare);

		/* de-dupe as we copy from the batch in-order */
		for (i = 0; i < nr_objects_batch; i++) {
			if (i > 0 && !oidcmp(&objects_batch[i - 1].oid, &objects_batch[i].oid))
				continue;

			ALLOC_GROW(local_objects, nr_local_objects + 1, alloc_objects);
			memcpy(&local_objects[nr_local_objects], &objects_batch[i], sizeof(struct pack_midx_entry));
			nr_local_objects++;
		}
	}

	*nr_objects = nr_local_objects;
	*objects = local_objects;
}

static int build_midx_from_packs(
	const char *pack_dir,
	const char **pack_names, uint32_t nr_packs,
	const char **midx_id, struct midxed_git *midx)
{
	struct packed_git **packs;
	const char **installed_pack_names;
	uint32_t i, nr_installed_packs = 0;
	uint32_t nr_objects = 0;
	struct pack_midx_entry *objects = NULL;
	uint32_t nr_total_packs = nr_packs;
	struct strbuf pack_path = STRBUF_INIT;
	int baselen;

	if (midx)
		nr_total_packs += midx->num_packs;

	ALLOC_ARRAY(packs, nr_total_packs);
	ALLOC_ARRAY(installed_pack_names, nr_total_packs);

	if (midx) {
		for (i = 0; i < midx->num_packs; i++)
			installed_pack_names[nr_installed_packs++] = midx->pack_names[i];
	}

	strbuf_addstr(&pack_path, pack_dir);
	strbuf_addch(&pack_path, '/');
	baselen = pack_path.len;
	for (i = 0; i < nr_packs; i++) {
		strbuf_setlen(&pack_path, baselen);
		strbuf_addstr(&pack_path, pack_names[i]);

		if (midx && contains_pack(midx, pack_names[i]))
			continue;

		strbuf_strip_suffix(&pack_path, ".pack");
		strbuf_addstr(&pack_path, ".idx");

		packs[nr_installed_packs] = add_packed_git(pack_path.buf, pack_path.len, 0);

		if (packs[nr_installed_packs] != NULL) {
			if (open_pack_index(packs[nr_installed_packs]))
				continue;

			nr_objects += packs[nr_installed_packs]->num_objects;
			installed_pack_names[nr_installed_packs] = pack_names[i];
			nr_installed_packs++;
		}
	}
	strbuf_release(&pack_path);

	if (!nr_objects || !nr_installed_packs) {
		if (opts.has_existing)
			*midx_id = oid_to_hex(&opts.old_midx_oid);
		else
			*midx_id = 0;
		free(packs);
		free(installed_pack_names);
		return 0;
	}

	dedupe_and_sort_entries(packs, nr_installed_packs,
				midx, &objects, &nr_objects);

	*midx_id = write_midx_file(pack_dir, NULL,
		installed_pack_names, nr_installed_packs,
		objects, nr_objects);

	FREE_AND_NULL(installed_pack_names);
	FREE_AND_NULL(objects);

	return 0;
}

static void update_head_file(const char *pack_dir, const char *midx_id)
{
	struct strbuf head_path = STRBUF_INIT;
	int fd;
	struct lock_file lk = LOCK_INIT;

	strbuf_addstr(&head_path, pack_dir);
	strbuf_addstr(&head_path, "/");
	strbuf_addstr(&head_path, "midx-head");

	fd = hold_lock_file_for_update(&lk, head_path.buf, LOCK_DIE_ON_ERROR);
	strbuf_release(&head_path);

	if (fd < 0)
		die_errno("unable to open midx-head");

	write_in_full(fd, midx_id, GIT_MAX_HEXSZ);
	commit_lock_file(&lk);
}

static int midx_write(void)
{
	const char **pack_names = NULL;
	uint32_t i, nr_packs = 0;
	const char *midx_id = 0;
	DIR *dir;
	struct dirent *de;
	struct midxed_git *midx = NULL;

	if (opts.has_existing)
		midx = get_midxed_git(opts.pack_dir, &opts.old_midx_oid);

	dir = opendir(opts.pack_dir);
	if (!dir) {
		error_errno("unable to open object pack directory: %s",
			opts.pack_dir);
		return 1;
	}

	nr_packs = 256;
	ALLOC_ARRAY(pack_names, nr_packs);

	i = 0;
	while ((de = readdir(dir)) != NULL) {
		if (is_dot_or_dotdot(de->d_name))
			continue;

		if (ends_with(de->d_name, ".pack")) {
			ALLOC_GROW(pack_names, i + 1, nr_packs);
			pack_names[i++] = xstrdup(de->d_name);
		}
	}

	nr_packs = i;
	closedir(dir);

	if (!nr_packs)
		goto cleanup;

	if (build_midx_from_packs(opts.pack_dir, pack_names, nr_packs, &midx_id, midx))
		die("failed to build MIDX");

	if (!midx_id)
		goto cleanup;

	printf("%s\n", midx_id);

	if (opts.update_head)
		update_head_file(opts.pack_dir, midx_id);

	if (opts.delete_expired && opts.update_head && opts.has_existing &&
	    strcmp(midx_id, oid_to_hex(&opts.old_midx_oid))) {
		char *old_path = get_midx_head_filename_oid(opts.pack_dir, &opts.old_midx_oid);
		close_midx(midx);
		if (remove_path(old_path))
			die("failed to remove path %s", old_path);

		free(old_path);
	}

cleanup:
	if (pack_names)
		FREE_AND_NULL(pack_names);
	return 0;
}

static int midx_read(void)
{
	struct object_id midx_oid;
	struct midxed_git *midx;
	uint32_t i;

	if (opts.midx_id && strlen(opts.midx_id) == GIT_MAX_HEXSZ)
		get_oid_hex(opts.midx_id, &midx_oid);
	else if (!get_midx_head_oid(opts.pack_dir, &midx_oid))
		die("No midx-head exists.");

	midx = get_midxed_git(opts.pack_dir, &midx_oid);

	printf("header: %08x %08x %02x %02x %02x %02x %08x\n",
		ntohl(midx->hdr->midx_signature),
		ntohl(midx->hdr->midx_version),
		midx->hdr->hash_version,
		midx->hdr->hash_len,
		midx->hdr->num_base_midx,
		midx->hdr->num_chunks,
		ntohl(midx->hdr->num_packs));
	printf("num_objects: %d\n", midx->num_objects);
	printf("chunks:");

	if (midx->chunk_pack_lookup)
		printf(" pack_lookup");
	if (midx->chunk_pack_names)
		printf(" pack_names");
	if (midx->chunk_oid_fanout)
		printf(" oid_fanout");
	if (midx->chunk_oid_lookup)
		printf(" oid_lookup");
	if (midx->chunk_object_offsets)
		printf(" object_offsets");
	if (midx->chunk_large_offsets)
		printf(" large_offsets");
	printf("\n");

	printf("pack_names:\n");
	for (i = 0; i < midx->num_packs; i++)
		printf("%s\n", midx->pack_names[i]);

	printf("pack_dir: %s\n", midx->pack_dir);
	return 0;
}

static int midx_clear(void)
{
	struct strbuf head_path = STRBUF_INIT;
	char *old_path;

	if (!opts.has_existing)
		return 0;

	strbuf_addstr(&head_path, opts.pack_dir);
	strbuf_addstr(&head_path, "/");
	strbuf_addstr(&head_path, "midx-head");
	if (remove_path(head_path.buf))
		die("failed to remove path %s", head_path.buf);
	strbuf_release(&head_path);

	old_path = get_midx_head_filename_oid(opts.pack_dir, &opts.old_midx_oid);
	if (remove_path(old_path))
		die("failed to remove path %s", old_path);
	free(old_path);

	return 0;
}

int cmd_midx(int argc, const char **argv, const char *prefix)
{
	static struct option builtin_midx_options[] = {
		{ OPTION_STRING, 'p', "pack-dir", &opts.pack_dir,
			N_("dir"),
			N_("The pack directory containing set of packfile and pack-index pairs.") },
		OPT_BOOL('w', "write", &opts.write,
			N_("write midx file")),
		OPT_BOOL('u', "update-head", &opts.update_head,
			N_("update midx-head to written midx file")),
		OPT_BOOL('d', "delete-expired", &opts.delete_expired,
			N_("delete expired head midx file")),
		OPT_BOOL('r', "read", &opts.read,
			N_("read midx file")),
		OPT_BOOL('c', "clear", &opts.clear,
			N_("clear midx file and midx-head")),
		OPT_BOOL(0, "verify", &opts.verify,
			N_("verify the contents of a midx file")),
		{ OPTION_STRING, 'M', "midx-id", &opts.midx_id,
			N_("oid"),
			N_("An OID for a specific midx file in the pack-dir."),
			PARSE_OPT_OPTARG, NULL, (intptr_t) "" },
		OPT_END(),
	};

	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage_with_options(builtin_midx_usage, builtin_midx_options);

	git_config(git_default_config, NULL);
	if (!core_midx)
		die("git-midx requires core.midx=true.");

	argc = parse_options(argc, argv, prefix,
			     builtin_midx_options,
			     builtin_midx_usage, 0);

	if (opts.write + opts.read + opts.clear + opts.verify > 1)
		usage_with_options(builtin_midx_usage, builtin_midx_options);

	if (!opts.pack_dir) {
		struct strbuf path = STRBUF_INIT;
		strbuf_addstr(&path, get_object_directory());
		strbuf_addstr(&path, "/pack");
		opts.pack_dir = strbuf_detach(&path, NULL);
	}

	opts.has_existing = !!get_midx_head_oid(opts.pack_dir, &opts.old_midx_oid);

	if (opts.write)
		return midx_write();
	if (opts.read)
		return midx_read();
	if (opts.clear)
		return midx_clear();
	if (opts.verify)
		return midx_verify(opts.pack_dir, opts.midx_id);

	return 0;
}
