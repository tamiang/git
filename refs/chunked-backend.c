#include "../cache.h"
#include "../config.h"
#include "../refs.h"
#include "refs-internal.h"
#include "chunked-backend.h"
#include "../iterator.h"
#include "../lockfile.h"
#include "../chdir-notify.h"
#include "../chunk-format.h"
#include "../csum-file.h"

static inline int chunked_enabled(void)
{
	/*
	 * This used to be gated on an environment variable, but the
	 * chunked backend became a full replacement of the packed
	 * backend so that environment variable is less important in
	 * this experiment.
	 */
	return 1;
}

/*
 * This value is set in `base.flags` if the peeled value of the
 * current reference is known. In that case, `peeled` contains the
 * correct peeled value for the reference, which might be `null_oid`
 * if the reference is not a tag or if it is broken.
 */
#define REF_KNOWS_PEELED 0x40

/* 4-byte identifiers for the chunked-refs format */
#define CHREFS_SIGNATURE               0x43524546 /* "CGPH" */
#define CHREFS_CHUNKID_OIDS            0x4F494453 /* "OIDS" */
#define CHREFS_CHUNKID_OFFSETS         0x524F4646 /* "ROFF" */
#define CHREFS_CHUNKID_REFS            0x52454653 /* "REFS" */
#define CHREFS_CHUNKID_PEELED_OFFSETS  0x504F4646 /* "POFF" */
#define CHREFS_CHUNKID_PEELED_OIDS     0x504F4944 /* "POID" */
#define NO_PEEL_EXISTS 0xFFFFFFFF

struct chunked_ref_store;

/*
 * A `snapshot` represents one snapshot of a `chunked-refs` file.
 *
 * Normally, this will be a mmapped view of the contents of the
 * `chunked-refs` file at the time the snapshot was created. However,
 * if the `chunked-refs` file was not sorted, this might point at heap
 * memory holding the contents of the `chunked-refs` file with its
 * records sorted by refname.
 *
 * `snapshot` instances are reference counted (via
 * `acquire_snapshot()` and `release_snapshot()`). This is to prevent
 * an instance from disappearing while an iterator is still iterating
 * over it. Instances are garbage collected when their `referrers`
 * count goes to zero.
 *
 * The most recent `snapshot`, if available, is referenced by the
 * `chunked_ref_store`. Its freshness is checked whenever
 * `get_snapshot()` is called; if the existing snapshot is obsolete, a
 * new snapshot is taken.
 */
struct chunked_snapshot {
	/*
	 * A back-pointer to the chunked_ref_store with which this
	 * snapshot is associated:
	 */
	struct chunked_ref_store *refs;

	/*
	 * The memory map for the chunked-refs file.
	 */
	unsigned char *mmap;
	size_t mmap_size;

	size_t nr;
	const unsigned char *offset_chunk;
	const char *refs_chunk;
	const unsigned char *oids_chunk;
	const unsigned char *peeled_offsets_chunk;
	const unsigned char *peeled_oids_chunk;

	/*
	 * Count of references to this instance, including the pointer
	 * from `chunked_ref_store::snapshot`, if any. The instance
	 * will not be freed as long as the reference count is
	 * nonzero.
	 */
	unsigned int referrers;

	/*
	 * The metadata of the `chunked-refs` file from which this
	 * snapshot was created, used to tell if the file has been
	 * replaced since we read it.
	 */
	struct stat_validity validity;
};

/*
 * A `ref_store` representing references stored in a `chunked-refs`
 * file. It implements the `ref_store` interface, though it has some
 * limitations:
 *
 * - It cannot store symbolic references.
 *
 * - It cannot store reflogs.
 *
 * - It does not support reference renaming (though it could).
 *
 * On the other hand, it can be locked outside of a reference
 * transaction. In that case, it remains locked even after the
 * transaction is done and the new `chunked-refs` file is activated.
 */
struct chunked_ref_store {
	struct ref_store base;

	unsigned int store_flags;

	/* The path of the "chunked-refs" file: */
	char *path;

	/*
	 * A snapshot of the values read from the `chunked-refs` file,
	 * if it might still be current; otherwise, NULL.
	 */
	struct chunked_snapshot *snapshot;

	/*
	 * Lock used for the "chunked-refs" file. Note that this (and
	 * thus the enclosing `chunked_ref_store`) must not be freed.
	 */
	struct lock_file lock;

	/*
	 * Temporary file used when rewriting new contents to the
	 * "chunked-refs" file. Note that this (and thus the enclosing
	 * `chunked_ref_store`) must not be freed.
	 */
	struct tempfile *tempfile;
};

/*
 * Increment the reference count of `*snapshot`.
 */
static void acquire_snapshot(struct chunked_snapshot *snapshot)
{
	snapshot->referrers++;
}

/*
 * If the memory map in `snapshot` is active, then unmap it and set
 * all pointers to NULL. Also clear the chunkfile.
 */
static void clear_snapshot_buffer(struct chunked_snapshot *snapshot)
{
	if (snapshot->mmap) {
		if (munmap(snapshot->mmap, snapshot->mmap_size))
			die_errno("error ummapping chunked-refs file %s",
				  snapshot->refs->path);
		snapshot->mmap = NULL;
		snapshot->mmap_size = 0;
	}
}

/*
 * Decrease the reference count of `*snapshot`. If it goes to zero,
 * free `*snapshot` and return true; otherwise return false.
 */
static int release_snapshot(struct chunked_snapshot *snapshot)
{
	if (!--snapshot->referrers) {
		stat_validity_clear(&snapshot->validity);
		clear_snapshot_buffer(snapshot);
		free(snapshot);
		return 1;
	} else {
		return 0;
	}
}

struct ref_store *chunked_ref_store_create(struct repository *repo,
					  const char *gitdir,
					  unsigned int store_flags)
{
	struct chunked_ref_store *refs;
	struct ref_store *ref_store;
	struct strbuf sb = STRBUF_INIT;

	if (!chunked_enabled())
		return NULL;

	refs = xcalloc(1, sizeof(*refs));
	ref_store = (struct ref_store *)refs;

	base_ref_store_init(ref_store, repo, gitdir, &refs_be_chunked);
	refs->store_flags = store_flags;

	strbuf_addf(&sb, "%s/chunked-refs", gitdir);
	refs->path = strbuf_detach(&sb, NULL);
	chdir_notify_reparent("chunked-refs", &refs->path);
	return ref_store;
}

/*
 * Downcast `ref_store` to `chunked_ref_store`. Die if `ref_store` is
 * not a `chunked_ref_store`. Also die if `chunked_ref_store` doesn't
 * support at least the flags specified in `required_flags`. `caller`
 * is used in any necessary error messages.
 */
static struct chunked_ref_store *chunked_downcast(struct ref_store *ref_store,
						unsigned int required_flags,
						const char *caller)
{
	struct chunked_ref_store *refs;

	if (ref_store->be != &refs_be_chunked)
		BUG("ref_store is type \"%s\" not \"chunked\" in %s",
		    ref_store->be->name, caller);

	refs = (struct chunked_ref_store *)ref_store;

	if ((refs->store_flags & required_flags) != required_flags)
		BUG("unallowed operation (%s), requires %x, has %x\n",
		    caller, required_flags, refs->store_flags);

	return refs;
}

static void clear_snapshot(struct chunked_ref_store *refs)
{
	if (refs->snapshot) {
		struct chunked_snapshot *snapshot = refs->snapshot;

		refs->snapshot = NULL;
		release_snapshot(snapshot);
	}
}

struct chunked_snapshot_record {
	size_t row;
	char *ref_name;
};

/*
 * Load the contents of the chunked-refs file into the snapshot and
 * populate the mmap and mmap_size parameters properly.
 */
static int load_contents(struct chunked_snapshot *snapshot)
{
	int fd;
	struct stat st;

	fd = open(snapshot->refs->path, O_RDONLY);
	if (fd < 0) {
		if (errno == ENOENT) {
			/*
			 * This is OK; it just means that no
			 * "chunked-refs" file has been written yet,
			 * which is equivalent to it being empty,
			 * which is its state when initialized with
			 * zeros.
			 */
			return 0;
		} else {
			die_errno("couldn't read %s", snapshot->refs->path);
		}
	}

	stat_validity_update(&snapshot->validity, fd);

	if (fstat(fd, &st) < 0)
		die_errno("couldn't stat %s", snapshot->refs->path);
	snapshot->mmap_size = xsize_t(st.st_size);

	if (!snapshot->mmap_size) {
		close(fd);
		return 0;
	} else {
		snapshot->mmap = xmmap(NULL, snapshot->mmap_size,
				       PROT_READ, MAP_PRIVATE, fd, 0);
	}
	close(fd);

	return 1;
}

static const char *get_nth_ref(struct chunked_snapshot *snapshot,
			       size_t n)
{
	uint64_t offset;

	if (n >= snapshot->nr)
		BUG("asking for position %"PRIuMAX" outside of bounds (%"PRIuMAX")",
		    n, snapshot->nr);

	offset = get_be64(snapshot->offset_chunk + n * sizeof(uint64_t));
	return snapshot->refs_chunk + offset;
}

/*
 * Find the place in `snapshot->buf` where the start of the record for
 * `refname` starts. If `mustexist` is true and the reference doesn't
 * exist, then return NULL. If `mustexist` is false and the reference
 * doesn't exist, then return the point where that reference would be
 * inserted, or `snapshot->eof` (which might be NULL) if it would be
 * inserted at the end of the file. In the latter mode, `refname`
 * doesn't have to be a proper reference name; for example, one could
 * search for "refs/replace/" to find the start of any replace
 * references.
 *
 * The record is sought using a binary search, so `snapshot->buf` must
 * be sorted.
 */
static const char *find_reference_location(struct chunked_snapshot *snapshot,
					   const char *refname, int mustexist,
					   size_t *pos)
{
	size_t lo = 0, hi = snapshot->nr;

	while (lo != hi) {
		const char *rec;
		int cmp;
		size_t mid = lo + (hi - lo) / 2;

		rec = get_nth_ref(snapshot, mid);
		cmp = strcmp(rec, refname);
		if (cmp < 0) {
			lo = mid + 1;
		} else if (cmp > 0) {
			hi = mid;
		} else {
			*pos = mid;
			return rec;
		}
	}

	if (mustexist) {
		return NULL;
	} else {
		/*
		 * We are likely doing a prefix match, so use the current
		 * 'lo' position as the indicator.
		 */
		*pos = lo;
		return get_nth_ref(snapshot, lo);
	}
}


static int chunked_refs_read_offsets(const unsigned char *chunk_start,
				     size_t chunk_size, void *data)
{
	struct chunked_snapshot *snapshot = data;

	snapshot->offset_chunk = chunk_start;
	snapshot->nr = chunk_size / sizeof(uint64_t);
	return 0;
}

/*
 * Create a newly-allocated `snapshot` of the `chunked-refs` file in
 * its current state and return it. The return value will already have
 * its reference count incremented.
 */
static struct chunked_snapshot *create_snapshot(struct chunked_ref_store *refs)
{
	struct chunked_snapshot *snapshot = xcalloc(1, sizeof(*snapshot));
	struct chunkfile *cf = NULL;
	uint32_t file_signature, hash_version;

	snapshot->refs = refs;
	acquire_snapshot(snapshot);

	if (!load_contents(snapshot))
		return snapshot;

	file_signature = get_be32(snapshot->mmap);
	if (file_signature != CHREFS_SIGNATURE)
		die(_("%s file signature %X does not match signature %X"),
		    "chunked-ref", file_signature, CHREFS_SIGNATURE);

	hash_version = get_be32(snapshot->mmap + sizeof(uint32_t));
	if (hash_version != the_hash_algo->format_id)
		die(_("hash version %X does not match expected hash version %X"),
		    hash_version, the_hash_algo->format_id);

	cf = init_chunkfile(NULL);

	if (read_trailing_table_of_contents(cf, snapshot->mmap, snapshot->mmap_size)) {
		release_snapshot(snapshot);
		snapshot = NULL;
		goto cleanup;
	}

	read_chunk(cf, CHREFS_CHUNKID_OFFSETS, chunked_refs_read_offsets, snapshot);
	pair_chunk(cf, CHREFS_CHUNKID_REFS, (const unsigned char**)&snapshot->refs_chunk);
	pair_chunk(cf, CHREFS_CHUNKID_OIDS, &snapshot->oids_chunk);
	pair_chunk(cf, CHREFS_CHUNKID_PEELED_OFFSETS, &snapshot->peeled_offsets_chunk);
	pair_chunk(cf, CHREFS_CHUNKID_PEELED_OIDS, &snapshot->peeled_oids_chunk);

cleanup:
	free_chunkfile(cf);
	return snapshot;
}

/*
 * Check that `refs->snapshot` (if present) still reflects the
 * contents of the `chunked-refs` file. If not, clear the snapshot.
 */
static void validate_snapshot(struct chunked_ref_store *refs)
{
	if (refs->snapshot &&
	    !stat_validity_check(&refs->snapshot->validity, refs->path))
		clear_snapshot(refs);
}

/*
 * Get the `snapshot` for the specified chunked_ref_store, creating and
 * populating it if it hasn't been read before or if the file has been
 * changed (according to its `validity` field) since it was last read.
 * On the other hand, if we hold the lock, then assume that the file
 * hasn't been changed out from under us, so skip the extra `stat()`
 * call in `stat_validity_check()`. This function does *not* increase
 * the snapshot's reference count on behalf of the caller.
 */
static struct chunked_snapshot *get_snapshot(struct chunked_ref_store *refs)
{
	if (!is_lock_file_locked(&refs->lock))
		validate_snapshot(refs);

	if (!refs->snapshot)
		refs->snapshot = create_snapshot(refs);

	return refs->snapshot;
}

static int chunked_read_raw_ref(struct ref_store *ref_store, const char *refname,
			       struct object_id *oid, struct strbuf *referent,
			       unsigned int *type, int *failure_errno)
{
	struct chunked_ref_store *refs =
		chunked_downcast(ref_store, REF_STORE_READ, "read_raw_ref");
	struct chunked_snapshot *snapshot = get_snapshot(refs);
	const char *rec;
	size_t ref_pos;
	const unsigned char *oid_pos;

	if (!chunked_enabled())
		return -1;

	*type = 0;

	rec = find_reference_location(snapshot, refname, 1, &ref_pos);

	if (!rec) {
		/* refname is not a chunked reference. */
		*failure_errno = ENOENT;
		return -1;
	}

	oid_pos = snapshot->oids_chunk + ref_pos * the_hash_algo->rawsz;
	hashcpy(oid->hash, oid_pos);
	oid->algo = hash_algo_by_ptr(the_hash_algo);

	*type = REF_ISCHUNKED;
	return 0;
}

/*
 * An iterator over a snapshot of a `chunked-refs` file.
 */
struct chunked_ref_iterator {
	struct ref_iterator base;

	struct chunked_snapshot *snapshot;

	const char *ref_pos;
	const unsigned char *oid_pos;
	const unsigned char *peeled_pos;

	/* The end of the oids chunk that will be iterated over. */
	const unsigned char *end_of_oids;

	/* Scratch space for current values: */
	struct object_id oid, peeled;
	struct strbuf refname_buf;

	struct repository *repo;
	unsigned int flags;
};

/*
 * Move the iterator to the next record in the snapshot, without
 * respect for whether the record is actually required by the current
 * iteration. Adjust the fields in `iter` and return `ITER_OK` or
 * `ITER_DONE`. This function does not free the iterator in the case
 * of `ITER_DONE`.
 */
static int next_record(struct chunked_ref_iterator *iter)
{
	uint32_t peel_offset;
	strbuf_reset(&iter->refname_buf);

	if (iter->oid_pos == iter->end_of_oids)
		return ITER_DONE;

	iter->base.flags = REF_ISCHUNKED;

	strbuf_addstr(&iter->refname_buf, iter->ref_pos);
	iter->base.refname = iter->refname_buf.buf;

	hashcpy(iter->oid.hash, iter->oid_pos);
	iter->oid.algo = hash_algo_by_ptr(the_hash_algo);

	if (check_refname_format(iter->base.refname, REFNAME_ALLOW_ONELEVEL)) {
		if (!refname_is_safe(iter->base.refname))
			die("chunked refname is dangerous: %s",
			    iter->base.refname);
		oidclr(&iter->oid);
		iter->base.flags |= REF_BAD_NAME | REF_ISBROKEN;
	}
	if (starts_with(iter->base.refname, "refs/tags/"))
		iter->base.flags |= REF_KNOWS_PEELED;

	peel_offset = get_be32(iter->peeled_pos);
	if (peel_offset == NO_PEEL_EXISTS) {
		oidclr(&iter->peeled);
		iter->base.flags &= ~REF_KNOWS_PEELED;
	} else {
		const unsigned char *peeled_oid;

		peeled_oid = iter->snapshot->peeled_oids_chunk +
			     peel_offset * the_hash_algo->rawsz;
		hashcpy(iter->peeled.hash, peeled_oid);
		iter->peeled.algo = hash_algo_by_ptr(the_hash_algo);
		iter->base.flags |= REF_KNOWS_PEELED;
	}

	/* Move to next ref in all pointers. */
	iter->ref_pos += iter->refname_buf.len + 1;
	iter->oid_pos += the_hash_algo->rawsz;
	iter->peeled_pos += sizeof(uint32_t);

	return ITER_OK;
}

static int chunked_ref_iterator_advance(struct ref_iterator *ref_iterator)
{
	struct chunked_ref_iterator *iter =
		(struct chunked_ref_iterator *)ref_iterator;
	int ok;

	while ((ok = next_record(iter)) == ITER_OK) {
		if (iter->flags & DO_FOR_EACH_PER_WORKTREE_ONLY &&
		    !!is_per_worktree_ref(iter->base.refname))
			continue;

		if (!(iter->flags & DO_FOR_EACH_INCLUDE_BROKEN) &&
		    !ref_resolves_to_object(iter->base.refname, iter->repo,
					    &iter->oid, iter->flags))
			continue;

		return ITER_OK;
	}

	if (ref_iterator_abort(ref_iterator) != ITER_DONE)
		ok = ITER_ERROR;

	return ok;
}

static int chunked_ref_iterator_peel(struct ref_iterator *ref_iterator,
				     struct object_id *peeled)
{
	struct chunked_ref_iterator *iter =
		(struct chunked_ref_iterator *)ref_iterator;

	if (iter->repo != the_repository)
		BUG("peeling for non-the_repository is not supported");

	if ((iter->base.flags & REF_KNOWS_PEELED)) {
		oidcpy(peeled, &iter->peeled);
		return is_null_oid(&iter->peeled) ? -1 : 0;
	} else if ((iter->base.flags & (REF_ISBROKEN | REF_ISSYMREF))) {
		return -1;
	} else {
		return peel_object(&iter->oid, peeled) ? -1 : 0;
	}
}

static int chunked_ref_iterator_abort(struct ref_iterator *ref_iterator)
{
	struct chunked_ref_iterator *iter =
		(struct chunked_ref_iterator *)ref_iterator;
	int ok = ITER_DONE;

	strbuf_release(&iter->refname_buf);
	release_snapshot(iter->snapshot);
	base_ref_iterator_free(ref_iterator);
	return ok;
}

static struct ref_iterator_vtable chunked_ref_iterator_vtable = {
	.advance = chunked_ref_iterator_advance,
	.peel = chunked_ref_iterator_peel,
	.abort = chunked_ref_iterator_abort,
};

static struct ref_iterator *chunked_ref_iterator_begin(
		struct ref_store *ref_store,
		const char *prefix, unsigned int flags)
{
	struct chunked_ref_store *refs;
	struct chunked_snapshot *snapshot;
	const char *start;
	struct chunked_ref_iterator *iter;
	struct ref_iterator *ref_iterator;
	unsigned int required_flags = REF_STORE_READ;
	size_t start_pos;

	if (!chunked_enabled())
		return NULL;

	if (!(flags & DO_FOR_EACH_INCLUDE_BROKEN))
		required_flags |= REF_STORE_ODB;
	refs = chunked_downcast(ref_store, required_flags, "ref_iterator_begin");

	/*
	 * Note that `get_snapshot()` internally checks whether the
	 * snapshot is up to date with what is on disk, and re-reads
	 * it if not.
	 */
	snapshot = get_snapshot(refs);

	if (!snapshot->nr)
		return empty_ref_iterator_begin();

	if (prefix && *prefix) {
		start = find_reference_location(snapshot, prefix, 0, &start_pos);
	} else {
		start = snapshot->refs_chunk;
		start_pos = 0;
	}

	if (start_pos == snapshot->nr)
		return empty_ref_iterator_begin();

	CALLOC_ARRAY(iter, 1);
	ref_iterator = &iter->base;
	base_ref_iterator_init(ref_iterator, &chunked_ref_iterator_vtable, 1);

	iter->snapshot = snapshot;
	acquire_snapshot(snapshot);

	iter->ref_pos = start;
	iter->oid_pos = snapshot->oids_chunk + start_pos * the_hash_algo->rawsz;
	iter->end_of_oids = snapshot->oids_chunk + snapshot->nr * the_hash_algo->rawsz;
	iter->peeled_pos = snapshot->peeled_offsets_chunk + start_pos * sizeof(uint32_t);

	strbuf_init(&iter->refname_buf, 0);

	iter->base.oid = &iter->oid;

	iter->repo = ref_store->repo;
	iter->flags = flags;

	if (prefix && *prefix)
		/* Stop iteration after we've gone *past* prefix: */
		ref_iterator = prefix_ref_iterator_begin(ref_iterator, prefix, 0);

	return ref_iterator;
}

int chunked_refs_lock(struct ref_store *ref_store, int flags, struct strbuf *err)
{
	struct chunked_ref_store *refs =
		chunked_downcast(ref_store, REF_STORE_WRITE | REF_STORE_MAIN,
				"chunked_refs_lock");
	static int timeout_configured = 0;
	static int timeout_value = 1000;

	if (!timeout_configured) {
		git_config_get_int("core.chunkedrefstimeout", &timeout_value);
		timeout_configured = 1;
	}

	/*
	 * Note that we close the lockfile immediately because we
	 * don't write new content to it, but rather to a separate
	 * tempfile.
	 */
	if (hold_lock_file_for_update_timeout(
			    &refs->lock,
			    refs->path,
			    flags, timeout_value) < 0) {
		unable_to_lock_message(refs->path, errno, err);
		return -1;
	}

	if (close_lock_file_gently(&refs->lock)) {
		strbuf_addf(err, "unable to close %s: %s", refs->path, strerror(errno));
		rollback_lock_file(&refs->lock);
		return -1;
	}

	/*
	 * There is a stat-validity problem might cause `update-ref -d`
	 * lost the newly commit of a ref, because a new `chunked-refs`
	 * file might has the same on-disk file attributes such as
	 * timestamp, file size and inode value, but has a changed
	 * ref value.
	 *
	 * This could happen with a very small chance when
	 * `update-ref -d` is called and at the same time another
	 * `pack-refs --all` process is running.
	 *
	 * Now that we hold the `chunked-refs` lock, it is important
	 * to make sure we could read the latest version of
	 * `chunked-refs` file no matter we have just mmap it or not.
	 * So what need to do is clear the snapshot if we hold it
	 * already.
	 */
	clear_snapshot(refs);

	/*
	 * Now make sure that the chunked-refs file as it exists in the
	 * locked state is loaded into the snapshot:
	 */
	get_snapshot(refs);
	return 0;
}

void chunked_refs_unlock(struct ref_store *ref_store)
{
	struct chunked_ref_store *refs = chunked_downcast(
			ref_store,
			REF_STORE_READ | REF_STORE_WRITE,
			"chunked_refs_unlock");

	if (!is_lock_file_locked(&refs->lock))
		BUG("chunked_refs_unlock() called when not locked");
	rollback_lock_file(&refs->lock);
}

int chunked_refs_is_locked(struct ref_store *ref_store)
{
	struct chunked_ref_store *refs = chunked_downcast(
			ref_store,
			REF_STORE_READ | REF_STORE_WRITE,
			"chunked_refs_is_locked");

	return is_lock_file_locked(&refs->lock);
}

static int chunked_init_db(struct ref_store *ref_store, struct strbuf *err)
{
	/* Nothing to do. */
	return 0;
}

struct chunked_refs_write_context {
	/* Parameters from write_with_updates(). */
	struct chunked_ref_store *refs;
	struct string_list *updates;
	struct strbuf *err;

	/*
	 * As we stream the ref names to the refs chunk, store these
	 * values in-memory. These arrays are populated one for every ref.
	 */
	uint32_t *peel_indexes;
	size_t *offsets;
	struct object_id *oids;
	size_t nr;
	size_t peel_alloc;
	size_t offsets_alloc;
	size_t oids_alloc;

	/*
	 * Only the peeled refs populate this array.
	 */
	struct object_id *peeled;
	size_t peeled_nr, peeled_alloc;
};

static void clear_write_context(struct chunked_refs_write_context *ctx) {
	free(ctx->peel_indexes);
	free(ctx->offsets);
	free(ctx->oids);
	free(ctx->peeled);
}

static int write_ref_and_update_arrays(struct hashfile *f,
				       struct chunked_refs_write_context *ctx,
				       const char *refname,
				       const struct object_id *oid,
				       const struct object_id *peeled)
{
	size_t len = strlen(refname) + 1;
	size_t i = ctx->nr;

	trace2_timer_start(TRACE2_TIMER_ID_ALLOCS);
	ALLOC_GROW(ctx->peel_indexes, i + 1, ctx->peel_alloc);
	ALLOC_GROW(ctx->offsets, i + 1, ctx->offsets_alloc);
	ALLOC_GROW(ctx->oids, i + 1, ctx->oids_alloc);
	trace2_timer_stop(TRACE2_TIMER_ID_ALLOCS);

	/* Write entire ref, including null terminator. */
	trace2_timer_start(TRACE2_TIMER_ID_HASHWRITE);
	hashwrite(f, refname, len);
	trace2_timer_stop(TRACE2_TIMER_ID_HASHWRITE);

	trace2_timer_start(TRACE2_TIMER_ID_COPIES);
	oidcpy(&ctx->oids[i], oid);
	if (i)
		ctx->offsets[i] = ctx->offsets[i - 1] + len;
	else
		ctx->offsets[i] = len;

	if (peeled) {
		ALLOC_GROW(ctx->peeled, ctx->peeled_nr + 1, ctx->peeled_alloc);
		oidcpy(&ctx->peeled[ctx->peeled_nr], peeled);
		ctx->peel_indexes[i] = ctx->peeled_nr;
		ctx->peeled_nr++;
	} else {
		ctx->peel_indexes[i] = NO_PEEL_EXISTS;
	}
	trace2_timer_stop(TRACE2_TIMER_ID_COPIES);

	ctx->nr++;
	return 0;
}

static int write_refs_chunk_refs(struct hashfile *f,
				 void *data)
{
	struct chunked_refs_write_context *ctx = data;
	struct ref_iterator *iter = NULL;
	size_t i;
	int ok;

	/* TODO: this merge of iterators can be abstracted! */
	trace2_region_enter("refs", "refs-chunk", the_repository);

	/*
	 * We iterate in parallel through the current list of refs and
	 * the list of updates, processing an entry from at least one
	 * of the lists each time through the loop. When the current
	 * list of refs is exhausted, set iter to NULL. When the list
	 * of updates is exhausted, leave i set to updates->nr.
	 */
	iter = chunked_ref_iterator_begin(&ctx->refs->base, "",
					  DO_FOR_EACH_INCLUDE_BROKEN);
	if ((ok = ref_iterator_advance(iter)) != ITER_OK)
		iter = NULL;

	i = 0;

	while (iter || i < ctx->updates->nr) {
		struct ref_update *update = NULL;
		int cmp;

		if (i >= ctx->updates->nr) {
			cmp = -1;
		} else {
			update = ctx->updates->items[i].util;

			if (!iter)
				cmp = +1;
			else
				cmp = strcmp(iter->refname, update->refname);
		}

		if (!cmp) {
			/*
			 * There is both an old value and an update
			 * for this reference. Check the old value if
			 * necessary:
			 */
			if ((update->flags & REF_HAVE_OLD)) {
				if (is_null_oid(&update->old_oid)) {
					strbuf_addf(ctx->err, "cannot update ref '%s': "
						    "reference already exists",
						    update->refname);
					goto error;
				} else if (!oideq(&update->old_oid, iter->oid)) {
					strbuf_addf(ctx->err, "cannot update ref '%s': "
						    "is at %s but expected %s",
						    update->refname,
						    oid_to_hex(iter->oid),
						    oid_to_hex(&update->old_oid));
					goto error;
				}
			}

			/* Now figure out what to use for the new value: */
			if ((update->flags & REF_HAVE_NEW)) {
				/*
				 * The update takes precedence. Skip
				 * the iterator over the unneeded
				 * value.
				 */
				if ((ok = ref_iterator_advance(iter)) != ITER_OK)
					iter = NULL;
				cmp = +1;
			} else {
				/*
				 * The update doesn't actually want to
				 * change anything. We're done with it.
				 */
				i++;
				cmp = -1;
			}
		} else if (cmp > 0) {
			/*
			 * There is no old value but there is an
			 * update for this reference. Make sure that
			 * the update didn't expect an existing value:
			 */
			if ((update->flags & REF_HAVE_OLD) &&
			    !is_null_oid(&update->old_oid)) {
				strbuf_addf(ctx->err, "cannot update ref '%s': "
					    "reference is missing but expected %s",
					    update->refname,
					    oid_to_hex(&update->old_oid));
				goto error;
			}
		}

		if (cmp < 0) {
			/* Pass the old reference through. */

			struct object_id peeled;
			int peel_error = ref_iterator_peel(iter, &peeled);

			if (write_ref_and_update_arrays(f, ctx, iter->refname,
							iter->oid,
							peel_error ? NULL : &peeled))
				goto write_error;

			if ((ok = ref_iterator_advance(iter)) != ITER_OK)
				iter = NULL;
		} else if (is_null_oid(&update->new_oid)) {
			/*
			 * The update wants to delete the reference,
			 * and the reference either didn't exist or we
			 * have already skipped it. So we're done with
			 * the update (and don't have to write
			 * anything).
			 */
			i++;
		} else {
			struct object_id peeled;
			int peel_error = peel_object(&update->new_oid,
						     &peeled);

			if (write_ref_and_update_arrays(f, ctx, update->refname,
							&update->new_oid,
							peel_error ? NULL : &peeled))
				goto write_error;

			i++;
		}
	}

	if (ok != ITER_DONE) {
		strbuf_addstr(ctx->err, "unable to write packed-refs file: "
			      "error iterating over old contents");
		goto error;
	} else if (ctx->nr) {
		char padding[3] = { 0, 0, 0 };
		/* Do we need to add padding to get 4-byte alignment? */
		size_t remainder = ctx->offsets[ctx->nr - 1] % sizeof(uint32_t);
		size_t padlen = (sizeof(uint32_t) - remainder) % sizeof(uint32_t);
		hashwrite(f, padding, padlen);
	}

	trace2_region_leave("refs", "refs-chunk", the_repository);

	return 0;

write_error:
	strbuf_addf(ctx->err, "error writing to %s: %s",
		    get_tempfile_path(ctx->refs->tempfile), strerror(errno));

error:
	if (iter)
		ref_iterator_abort(iter);
	return -1;
}

static int write_refs_chunk_oids(struct hashfile *f,
				 void *data)
{
	struct chunked_refs_write_context *ctx = data;
	size_t i;

	trace2_region_enter("refs", "oids-chunk", the_repository);
	for (i = 0; i < ctx->nr; i++)
		hashwrite(f, ctx->oids[i].hash, the_hash_algo->rawsz);

	trace2_region_leave("refs", "oids-chunk", the_repository);
	return 0;
}

static int write_refs_chunk_offsets(struct hashfile *f,
				    void *data)
{
	struct chunked_refs_write_context *ctx = data;
	size_t i;

	trace2_region_enter("refs", "offsets", the_repository);
	for (i = 0; i < ctx->nr; i++)
		hashwrite_be64(f, ctx->offsets[i]);

	trace2_region_leave("refs", "offsets", the_repository);
	return 0;
}

static int write_refs_chunk_peeled_offsets(struct hashfile *f,
					   void *data)
{
	struct chunked_refs_write_context *ctx = data;
	size_t i;

	trace2_region_enter("refs", "peeled-offsets", the_repository);
	for (i = 0; i < ctx->nr; i++)
		hashwrite_be32(f, ctx->peel_indexes[i]);

	trace2_region_leave("refs", "peeled-offsets", the_repository);
	return 0;
}

static int write_refs_chunk_peeled_oids(struct hashfile *f,
					void *data)
{
	struct chunked_refs_write_context *ctx = data;
	size_t i;

	trace2_region_enter("refs", "peeled-oids", the_repository);
	for (i = 0; i < ctx->peeled_nr; i++)
		hashwrite(f, ctx->peeled[i].hash, the_hash_algo->rawsz);

	trace2_region_leave("refs", "peeled-oids", the_repository);
	return 0;
}

/*
 * Write the chunked refs from the current snapshot to the chunked-refs
 * tempfile, incorporating any changes from `updates`. `updates` must
 * be a sorted string list whose keys are the refnames and whose util
 * values are `struct ref_update *`. On error, rollback the tempfile,
 * write an error message to `err`, and return a nonzero value.
 *
 * The packfile must be locked before calling this function and will
 * remain locked when it is done.
 */
static int write_with_updates(struct chunked_ref_store *refs,
			      struct string_list *updates,
			      struct strbuf *err)
{
	FILE *out;
	int fd;
	struct strbuf sb = STRBUF_INIT;
	char *chunked_refs_path;
	struct hashfile *f;
	struct chunkfile *cf;
	unsigned char file_hash[GIT_MAX_RAWSZ];
	int res;
	struct chunked_refs_write_context ctx = {
		.refs = refs,
		.updates = updates,
		.err = err,
	};

	if (!is_lock_file_locked(&refs->lock))
		BUG("write_with_updates() called while unlocked");

	/*
	 * If chunked-refs is a symlink, we want to overwrite the
	 * symlinked-to file, not the symlink itself. Also, put the
	 * staging file next to it:
	 */
	chunked_refs_path = get_locked_file_path(&refs->lock);
	strbuf_addf(&sb, "%s.new", chunked_refs_path);
	free(chunked_refs_path);
	refs->tempfile = create_tempfile(sb.buf);
	if (!refs->tempfile) {
		strbuf_addf(err, "unable to create file %s: %s",
			    sb.buf, strerror(errno));
		strbuf_release(&sb);
		return -1;
	}
	strbuf_release(&sb);

	out = fdopen_tempfile(refs->tempfile, "w");
	if (!out) {
		strbuf_addf(err, "unable to fdopen chunked-refs tempfile: %s",
			    strerror(errno));
		res = -1;
		goto cleanup;
	}

	fd = get_tempfile_fd(refs->tempfile);
	f = hashfd(fd, refs->tempfile->filename.buf);
	cf = init_chunkfile(f);

	add_chunk(cf, CHREFS_CHUNKID_REFS, 0, write_refs_chunk_refs);
	add_chunk(cf, CHREFS_CHUNKID_OIDS, 0, write_refs_chunk_oids);
	add_chunk(cf, CHREFS_CHUNKID_OFFSETS, 0, write_refs_chunk_offsets);
	add_chunk(cf, CHREFS_CHUNKID_PEELED_OFFSETS, 0, write_refs_chunk_peeled_offsets);
	add_chunk(cf, CHREFS_CHUNKID_PEELED_OIDS, 0, write_refs_chunk_peeled_oids);

	hashwrite_be32(f, CHREFS_SIGNATURE);
	hashwrite_be32(f, the_hash_algo->format_id);

	if (write_chunkfile(cf, CHUNKFILE_TRAILING_TOC, &ctx)) {
		delete_tempfile(&refs->tempfile);
		res = -1;
		goto cleanup;
	}

	finalize_hashfile(f, file_hash, FSYNC_COMPONENT_COMMIT_GRAPH,
			  CSUM_HASH_IN_STREAM | CSUM_FSYNC);
	free_chunkfile(cf);

	if (fsync_component(FSYNC_COMPONENT_REFERENCE, get_tempfile_fd(refs->tempfile)) ||
	    close_tempfile_gently(refs->tempfile)) {
		strbuf_addf(err, "error closing file %s: %s",
			    get_tempfile_path(refs->tempfile),
			    strerror(errno));
		strbuf_release(&sb);
		delete_tempfile(&refs->tempfile);
		res = -1;
		goto cleanup;
	}

	res = 0;

cleanup:
	clear_write_context(&ctx);
	return res;
}

int is_chunked_transaction_needed(struct ref_store *ref_store,
				 struct ref_transaction *transaction)
{
	struct chunked_ref_store *refs = chunked_downcast(
			ref_store,
			REF_STORE_READ,
			"is_chunked_transaction_needed");
	struct strbuf referent = STRBUF_INIT;
	size_t i;
	int ret;

	if (!is_lock_file_locked(&refs->lock))
		BUG("is_chunked_transaction_needed() called while unlocked");

	/*
	 * We're only going to bother returning false for the common,
	 * trivial case that references are only being deleted, their
	 * old values are not being checked, and the old `chunked-refs`
	 * file doesn't contain any of those reference(s). This gives
	 * false positives for some other cases that could
	 * theoretically be optimized away:
	 *
	 * 1. It could be that the old value is being verified without
	 *    setting a new value. In this case, we could verify the
	 *    old value here and skip the update if it agrees. If it
	 *    disagrees, we could either let the update go through
	 *    (the actual commit would re-detect and report the
	 *    problem), or come up with a way of reporting such an
	 *    error to *our* caller.
	 *
	 * 2. It could be that a new value is being set, but that it
	 *    is identical to the current chunked value of the
	 *    reference.
	 *
	 * Neither of these cases will come up in the current code,
	 * because the only caller of this function passes to it a
	 * transaction that only includes `delete` updates with no
	 * `old_id`. Even if that ever changes, false positives only
	 * cause an optimization to be missed; they do not affect
	 * correctness.
	 */

	/*
	 * Start with the cheap checks that don't require old
	 * reference values to be read:
	 */
	for (i = 0; i < transaction->nr; i++) {
		struct ref_update *update = transaction->updates[i];

		if (update->flags & REF_HAVE_OLD)
			/* Have to check the old value -> needed. */
			return 1;

		if ((update->flags & REF_HAVE_NEW) && !is_null_oid(&update->new_oid))
			/* Have to set a new value -> needed. */
			return 1;
	}

	/*
	 * The transaction isn't checking any old values nor is it
	 * setting any nonzero new values, so it still might be able
	 * to be skipped. Now do the more expensive check: the update
	 * is needed if any of the updates is a delete, and the old
	 * `chunked-refs` file contains a value for that reference.
	 */
	ret = 0;
	for (i = 0; i < transaction->nr; i++) {
		struct ref_update *update = transaction->updates[i];
		int failure_errno;
		unsigned int type;
		struct object_id oid;

		if (!(update->flags & REF_HAVE_NEW))
			/*
			 * This reference isn't being deleted -> not
			 * needed.
			 */
			continue;

		if (!refs_read_raw_ref(ref_store, update->refname, &oid,
				       &referent, &type, &failure_errno) ||
		    failure_errno != ENOENT) {
			/*
			 * We have to actually delete that reference
			 * -> this transaction is needed.
			 */
			ret = 1;
			break;
		}
	}

	strbuf_release(&referent);
	return ret;
}

struct chunked_transaction_backend_data {
	/* True iff the transaction owns the chunked-refs lock. */
	int own_lock;

	struct string_list updates;
};

static void chunked_transaction_cleanup(struct chunked_ref_store *refs,
				       struct ref_transaction *transaction)
{
	struct chunked_transaction_backend_data *data = transaction->backend_data;

	if (data) {
		string_list_clear(&data->updates, 0);

		if (is_tempfile_active(refs->tempfile))
			delete_tempfile(&refs->tempfile);

		if (data->own_lock && is_lock_file_locked(&refs->lock)) {
			chunked_refs_unlock(&refs->base);
			data->own_lock = 0;
		}

		free(data);
		transaction->backend_data = NULL;
	}

	transaction->state = REF_TRANSACTION_CLOSED;
}

static int chunked_transaction_prepare(struct ref_store *ref_store,
				      struct ref_transaction *transaction,
				      struct strbuf *err)
{
	struct chunked_ref_store *refs = chunked_downcast(
			ref_store,
			REF_STORE_READ | REF_STORE_WRITE | REF_STORE_ODB,
			"ref_transaction_prepare");
	struct chunked_transaction_backend_data *data;
	size_t i;
	int ret = TRANSACTION_GENERIC_ERROR;

	if (!chunked_enabled())
		return -1;

	/*
	 * Note that we *don't* skip transactions with zero updates,
	 * because such a transaction might be executed for the side
	 * effect of ensuring that all of the references are peeled or
	 * ensuring that the `chunked-refs` file is sorted. If the
	 * caller wants to optimize away empty transactions, it should
	 * do so itself.
	 */

	CALLOC_ARRAY(data, 1);
	string_list_init_nodup(&data->updates);

	transaction->backend_data = data;

	/*
	 * Stick the updates in a string list by refname so that we
	 * can sort them:
	 */
	for (i = 0; i < transaction->nr; i++) {
		struct ref_update *update = transaction->updates[i];
		struct string_list_item *item =
			string_list_append(&data->updates, update->refname);

		/* Store a pointer to update in item->util: */
		item->util = update;
	}
	string_list_sort(&data->updates);

	if (ref_update_reject_duplicates(&data->updates, err))
		goto failure;

	if (!is_lock_file_locked(&refs->lock)) {
		if (chunked_refs_lock(ref_store, 0, err))
			goto failure;
		data->own_lock = 1;
	}

	if (write_with_updates(refs, &data->updates, err))
		goto failure;

	transaction->state = REF_TRANSACTION_PREPARED;
	return 0;

failure:
	chunked_transaction_cleanup(refs, transaction);
	return ret;
}

static int chunked_transaction_abort(struct ref_store *ref_store,
				    struct ref_transaction *transaction,
				    struct strbuf *err)
{
	struct chunked_ref_store *refs = chunked_downcast(
			ref_store,
			REF_STORE_READ | REF_STORE_WRITE | REF_STORE_ODB,
			"ref_transaction_abort");

	if (!chunked_enabled())
		return -1;

	chunked_transaction_cleanup(refs, transaction);
	return 0;
}

static int chunked_transaction_finish(struct ref_store *ref_store,
				     struct ref_transaction *transaction,
				     struct strbuf *err)
{
	struct chunked_ref_store *refs = chunked_downcast(
			ref_store,
			REF_STORE_READ | REF_STORE_WRITE | REF_STORE_ODB,
			"ref_transaction_finish");
	int ret = TRANSACTION_GENERIC_ERROR;
	char *chunked_refs_path;

	if (!chunked_enabled())
		return -1;

	clear_snapshot(refs);

	chunked_refs_path = get_locked_file_path(&refs->lock);
	if (rename_tempfile(&refs->tempfile, chunked_refs_path)) {
		strbuf_addf(err, "error replacing %s: %s",
			    refs->path, strerror(errno));
		goto cleanup;
	}

	ret = 0;

cleanup:
	free(chunked_refs_path);
	chunked_transaction_cleanup(refs, transaction);
	return ret;
}

static int chunked_initial_transaction_commit(struct ref_store *ref_store,
					    struct ref_transaction *transaction,
					    struct strbuf *err)
{
	if (!chunked_enabled())
		return -1;

	return ref_transaction_commit(transaction, err);
}

static int chunked_delete_refs(struct ref_store *ref_store, const char *msg,
			     struct string_list *refnames, unsigned int flags)
{
	struct chunked_ref_store *refs =
		chunked_downcast(ref_store, REF_STORE_WRITE, "delete_refs");
	struct strbuf err = STRBUF_INIT;
	struct ref_transaction *transaction;
	struct string_list_item *item;
	int ret;

	if (!chunked_enabled())
		return -1;

	(void)refs; /* We need the check above, but don't use the variable */

	if (!refnames->nr)
		return 0;

	/*
	 * Since we don't check the references' old_oids, the
	 * individual updates can't fail, so we can pack all of the
	 * updates into a single transaction.
	 */

	transaction = ref_store_transaction_begin(ref_store, &err);
	if (!transaction)
		return -1;

	for_each_string_list_item(item, refnames) {
		if (ref_transaction_delete(transaction, item->string, NULL,
					   flags, msg, &err)) {
			warning(_("could not delete reference %s: %s"),
				item->string, err.buf);
			strbuf_reset(&err);
		}
	}

	ret = ref_transaction_commit(transaction, &err);

	if (ret) {
		if (refnames->nr == 1)
			error(_("could not delete reference %s: %s"),
			      refnames->items[0].string, err.buf);
		else
			error(_("could not delete references: %s"), err.buf);
	}

	ref_transaction_free(transaction);
	strbuf_release(&err);
	return ret;
}

static int chunked_pack_refs(struct ref_store *ref_store, unsigned int flags)
{
	/*
	 * chunked refs are already chunked. It might be that loose refs
	 * are chunked *into* a chunked refs store, but that is done by
	 * updating the chunked references via a transaction.
	 */
	return 0;
}

static struct ref_iterator *chunked_reflog_iterator_begin(struct ref_store *ref_store)
{
	return empty_ref_iterator_begin();
}

struct ref_storage_be refs_be_chunked = {
	.next = NULL,
	.name = "chunked",
	.init = chunked_ref_store_create,
	.init_db = chunked_init_db,
	.transaction_prepare = chunked_transaction_prepare,
	.transaction_finish = chunked_transaction_finish,
	.transaction_abort = chunked_transaction_abort,
	.initial_transaction_commit = chunked_initial_transaction_commit,

	.pack_refs = chunked_pack_refs,
	.create_symref = NULL,
	.delete_refs = chunked_delete_refs,
	.rename_ref = NULL,
	.copy_ref = NULL,

	.iterator_begin = chunked_ref_iterator_begin,
	.read_raw_ref = chunked_read_raw_ref,
	.read_symbolic_ref = NULL,

	.reflog_iterator_begin = chunked_reflog_iterator_begin,
	.for_each_reflog_ent = NULL,
	.for_each_reflog_ent_reverse = NULL,
	.reflog_exists = NULL,
	.create_reflog = NULL,
	.delete_reflog = NULL,
	.reflog_expire = NULL,
};
