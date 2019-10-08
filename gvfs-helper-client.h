#ifndef GVFS_HELPER_CLIENT_H
#define GVFS_HELPER_CLIENT_H

struct repository;
struct commit;

enum gh_client__created {
	/*
	 * The _get_ operation did not create anything.  If doesn't
	 * matter if `gvfs-helper` had errors or not -- just that
	 * nothing was created.
	 */
	GHC__CREATED__NOTHING  = 0,

	/*
	 * The _get_ operation created one or more packfiles.
	 */
	GHC__CREATED__PACKFILE = 1<<1,

	/*
	 * The _get_ operation created one or more loose objects.
	 * (Not necessarily the for the individual OID you requested.)
	 */
	GHC__CREATED__LOOSE    = 1<<2,

	/*
	 * The _get_ operation created one or more packfilea *and*
	 * one or more loose objects.
	 */
	GHC__CREATED__PACKFILE_AND_LOOSE = (GHC__CREATED__PACKFILE |
					    GHC__CREATED__LOOSE),
};

/*
 * Ask `gvfs-helper server` to immediately fetch an object.
 * Wait for the response.
 *
 * This may also fetch any queued (non-immediate) objects and
 * so may create one or more loose objects and/or packfiles.
 * It is undefined whether the requested OID will be loose or
 * in a packfile.
 */
int gh_client__get_immediate(const struct object_id *oid,
			     enum gh_client__created *p_ghc);

/*
 * Queue this OID for a future fetch using `gvfs-helper service`.
 * It does not wait.
 *
 * The GHC layer is free to process this queue in any way it wants,
 * including individual fetches, bulk fetches, and batching.  And
 * it may add queued objects to immediate requests.
 *
 * Callers should not rely on the queued object being on disk until
 * the queue has been drained.
 */
void gh_client__queue_oid(const struct object_id *oid);
void gh_client__queue_oid_array(const struct object_id *oids, int oid_nr);

int gh_client__drain_queue(enum gh_client__created *p_ghc);

#endif /* GVFS_HELPER_CLIENT_H */
