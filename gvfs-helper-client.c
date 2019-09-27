#include "cache.h"
#include "commit.h"
#include "argv-array.h"
#include "trace2.h"
#include "progress.h"
#include "oidset.h"
#include "revision.h"
#include "list-objects.h"
#include "list-objects-filter.h"
#include "list-objects-filter-options.h"
#include "object.h"
#include "object-store.h"
#include "bisect.h"
#include "gvfs-helper-client.h"
#include "sub-process.h"
#include "sigchain.h"
#include "pkt-line.h"
#include "quote.h"
#include "packfile.h"

static struct oidset ghc__oidset_queued = OIDSET_INIT;
static unsigned long ghc__oidset_count;

struct ghs__process {
	struct subprocess_entry subprocess; /* must be first */
	unsigned int supported_capabilities;
};

static int ghs__subprocess_map_initialized;
static struct hashmap ghs__subprocess_map;

#define CAP_GET      (1u<<1)

static int ghc__start_fn(struct subprocess_entry *subprocess)
{
	static int versions[] = {1, 0};
	static struct subprocess_capability capabilities[] = {
		{ "get", CAP_GET },
		{ NULL, 0 }
	};

	struct ghs__process *entry = (struct ghs__process *)subprocess;

	return subprocess_handshake(subprocess, "gvfs-helper", versions,
				    NULL, capabilities,
				    &entry->supported_capabilities);
}

/*
 * Send:
 *
 *     get LF
 *     (<hex-oid> LF)*
 *     <flush>
 *
 */
static int ghc__get__send_command(struct child_process *process)
{
	struct oidset_iter iter;
	struct object_id *oid;
	int err;

	/*
	 * We assume that all of the packet_ routines call error()
	 * so that we don't have to.
	 */

	err = packet_write_fmt_gently(process->in, "get\n");
	if (err)
		return err;

	oidset_iter_init(&ghc__oidset_queued, &iter);
	while ((oid = oidset_iter_next(&iter))) {
		err = packet_write_fmt_gently(process->in, "%s\n",
					      oid_to_hex(oid));
		if (err)
			return err;
	}

	err = packet_flush_gently(process->in);
	if (err)
		return err;

	return 0;
}

/*
 * Update the loose object cache to include the newly created
 * object.
 */
static void ghc__update_loose_cache(const char *line)
{
	const char *v1_oid;
	const char *v2_path;
	struct object_id oid;
	struct object_directory *odb;

	if (!skip_prefix(line, "loose ", &v1_oid))
		BUG("update_loose_cache: invalid line '%s'", line);

	if (parse_oid_hex(v1_oid, &oid, &v2_path))
		BUG("update_loose_cache: invalid oid in line '%s'", line);

	while (*v2_path == ' ')
		v2_path++;

	prepare_alt_odb(the_repository);
	for (odb = the_repository->objects->odb; odb; odb = odb->next)
		if (starts_with(v2_path, odb->path)) {
			odb_loose_cache_add_new_oid(odb, &oid);
			return;
		}

	// TODO If we fall back to the ".git/objects" odb, this
	// TODO bug will go off because we get a full path to the
	// TODO new loose object from gvfs-helper and the above
	// TODO "starts_with()" calls don't catch it.  Need to
	// TODO see if v2_path is inside $gitdir, for example.
	BUG("update_loose_cache: unknown odb in line '%s'", line);
}

/*
 * Update the packed-git list to include the newly created packfile.
 */
static void ghc__update_packed_git(const char *line)
{
	struct strbuf path = STRBUF_INIT;
	const char *v1_count;
	const char *v2_path;
	struct packed_git *p;
	int is_local;

	if (!skip_prefix(line, "packfile ", &v1_count))
		BUG("update_packed_git: invalid line '%s'", line);

	v2_path = v1_count;
	while (*v2_path && *v2_path != ' ')
		v2_path++;
	while (*v2_path == ' ')
		v2_path++;

	prepare_alt_odb(the_repository);

	/*
	 * ODB[0] is the local .git/objects.  All others are alternates.
	 */
	is_local = starts_with(v2_path, the_repository->objects->odb->path);

	strbuf_addstr(&path, v2_path);
	strbuf_strip_suffix(&path, ".pack");
	strbuf_addstr(&path, ".idx");

	p = add_packed_git(path.buf, path.len, is_local);
	if (p)
		install_packed_git_and_mru(the_repository, p);
}

/*
 * We expect:
 *
 *    <data>*
 *    <status>
 *    <flush>
 *
 * Where:
 *
 * <data>     ::= <packfile> / <loose>
 *
 * <packfile> ::= packfile SP <count> SP <pathname> LF
 *
 * <loose>    ::= loose SP <hex-oid> SP <pathname> LF
 *
 * <status>   ::=   ok LF
 *                / partial LF
 *                / error SP <message> LF
 *
 * Note that `gvfs-helper` controls how/if it chunks the request when
 * it talks to the cache-server and/or main Git server.  So it is
 * possible for us to receive many packfiles and/or loose objects *AND
 * THEN* get a hard network error or a 404 on an individual object.
 *
 * If we get a partial result, we can let the caller try to continue
 * -- for example, maybe an immediate request for a tree object was
 * grouped with a queued request for a blob.  The tree-walk *might* be
 * able to continue and let the 404 blob be handled later.
 */
static int ghc__get__receive_response(struct child_process *process,
				      enum ghc__created *p_ghc)
{
	enum ghc__created ghc = GHC__CREATED__NOTHING;
	const char *v1;
	char *line;
	int len;
	int err = 0;

	while (1) {
		/*
		 * Warning: packet_read_line_gently() calls die()
		 * despite the _gently moniker.
		 */
		len = packet_read_line_gently(process->out, NULL, &line);
		if ((len < 0) || !line)
			break;

		if (starts_with(line, "packfile")) {
			ghc__update_packed_git(line);
			ghc |= GHC__CREATED__PACKFILE;
		}

		else if (starts_with(line, "loose")) {
			ghc__update_loose_cache(line);
			ghc |= GHC__CREATED__LOOSE;
		}

		else if (starts_with(line, "ok"))
			;
		else if (starts_with(line, "partial"))
			;
		else if (skip_prefix(line, "error ", &v1)) {
			error("gvfs-helper error: '%s'", v1);
			err = -1;
		}
	}

	*p_ghc = ghc;

	return err;
}

static const char *ghc__created__debug_label(enum ghc__created ghc)
{
	switch (ghc) {
	case GHC__CREATED__NOTHING:
		return "nothing";
	case GHC__CREATED__PACKFILE:
		return "packfile";
	case GHC__CREATED__LOOSE:
		return "loose";
	case GHC__CREATED__PACKFILE_AND_LOOSE:
		return "packfile+loose";
	default:
		BUG("unknown GHC__CREATED value: %d", ghc);
		return "unknown";
	}
};

static int ghc__get(enum ghc__created *p_ghc)
{
	struct ghs__process *entry;
	struct child_process *process;
	struct argv_array argv = ARGV_ARRAY_INIT;
	struct strbuf quoted = STRBUF_INIT;
	int err = 0;

	trace2_region_enter("gh-client", "get", the_repository);

	/*
	 * TODO decide what defaults we want.
	 */
	argv_array_pushl(&argv,
			 "gvfs-helper",
			 "--mode=scalar",
			 "--fallback",
			 "--cache-server=trust",
			 "server",
			 NULL);
	sq_quote_argv_pretty(&quoted, argv.argv);

	if (!ghs__subprocess_map_initialized) {
		ghs__subprocess_map_initialized = 1;
		hashmap_init(&ghs__subprocess_map,
			     (hashmap_cmp_fn)cmd2process_cmp, NULL, 0);
		entry = NULL;
	} else
		entry = (struct ghs__process *)subprocess_find_entry(
			&ghs__subprocess_map, quoted.buf);

	if (!entry) {
		entry = xmalloc(sizeof(*entry));
		entry->supported_capabilities = 0;

		err = subprocess_start_argv(
			&ghs__subprocess_map, &entry->subprocess, 1,
			&argv, ghc__start_fn);
		if (err) {
			free(entry);
			goto leave_region;
		}
	}

	process = &entry->subprocess.process;

	if (!(CAP_GET & entry->supported_capabilities)) {
		error("gvfs-helper: does not support GET");
		subprocess_stop(&ghs__subprocess_map,
				(struct subprocess_entry *)entry);
		free(entry);
		err = -1;
		goto leave_region;
	}

	sigchain_push(SIGPIPE, SIG_IGN);

	err = ghc__get__send_command(process);
	if (!err)
		err = ghc__get__receive_response(process, p_ghc);

	sigchain_pop(SIGPIPE);

	if (err) {
		subprocess_stop(&ghs__subprocess_map,
				(struct subprocess_entry *)entry);
		free(entry);
	}

leave_region:
	argv_array_clear(&argv);
	strbuf_release(&quoted);

	trace2_data_intmax("gh-client", the_repository,
			   "get/count", ghc__oidset_count);
	if (err)
		trace2_data_intmax("gh-client", the_repository,
				   "get/error", err);
	trace2_data_string("gh-client", the_repository,
			   "get/created", ghc__created__debug_label(*p_ghc));
	trace2_region_leave("gh-client", "get", the_repository);

	oidset_clear(&ghc__oidset_queued);
	ghc__oidset_count = 0;

	return err;
}

void ghc__queue_oid(const struct object_id *oid)
{
	// TODO When we queue the first object, go ahead and spawn the helper
	// TODO so that it will be ready and listening while we are still
	// TODO accumulating queued objects.  That is, we don't need to wait
	// TODO for the drain call to spawn the process.

	trace2_printf("ghc__queue_oid: %s", oid_to_hex(oid));

	if (!oidset_insert(&ghc__oidset_queued, oid))
		ghc__oidset_count++;
}

/*
 * TODO This routine should actually take a "const struct oid_array *"
 * TODO rather than the component parts, but fetch_objects() uses
 * TODO this model (because of the call in sha1-file.c).
 */
void ghc__queue_oid_array(const struct object_id *oids, int oid_nr)
{
	int k;

	for (k = 0; k < oid_nr; k++)
		ghc__queue_oid(&oids[k]);
}

int ghc__drain_queue(enum ghc__created *p_ghc)
{
	*p_ghc = GHC__CREATED__NOTHING;

	if (!ghc__oidset_count)
		return 0;

	return ghc__get(p_ghc);
}

int ghc__get_immediate(const struct object_id *oid, enum ghc__created *p_ghc)
{
	trace2_printf("ghc__get_immediate: %s", oid_to_hex(oid));

	if (!oidset_insert(&ghc__oidset_queued, oid))
		ghc__oidset_count++;

	return ghc__drain_queue(p_ghc);
}
