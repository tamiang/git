#include "builtin.h"
#include "git-compat-util.h"
#include "config.h"
#include "parse-options.h"
#include "repository.h"
#include "commit.h"
#include "hex.h"
#include "tree.h"
#include "tree-walk.h"
#include "object.h"
#include "object-store-ll.h"
#include "oid-array.h"
#include "oidset.h"
#include "promisor-remote.h"
#include "strmap.h"
#include "string-list.h"
#include "revision.h"
#include "trace2.h"
#include "progress.h"
#include "packfile.h"
#include "gvfs-helper-client.h"
#include "path-walk.h"

static const char * const builtin_backfill_usage[] = {
	N_("git backfill [<options>]"),
	NULL
};

const size_t batch_size = 16000;

struct type_and_oid_list
{
	enum object_type type;
	struct oid_array oids;
};

#define TYPE_AND_OID_LIST_INIT { \
	.type = OBJ_NONE, 	 \
	.oids = OID_ARRAY_INIT	 \
}

static int download_batch(struct oid_array *batch)
{
	promisor_remote_get_direct(the_repository, batch->oid, batch->nr);
	oid_array_clear(batch);
	reprepare_packed_git(the_repository);
	return 0;
}


static int fill_missing_blobs(const char *path,
			      struct oid_array *list,
			      enum object_type type,
			      void *data)
{
	struct oid_array *batch = data;

	if (type != OBJ_BLOB)
		return 0;

	for (size_t i = 0; i < list->nr; i++) {
		struct object_info info = OBJECT_INFO_INIT;
		if (oid_object_info_extended(the_repository,
					     &list->oid[i],
					     &info,
					     OBJECT_INFO_FOR_PREFETCH))
			oid_array_append(batch, &list->oid[i]);
	}

	if (batch->nr >= batch_size)
		download_batch(batch);

	return 0;
}

static int do_backfill(void)
{
	struct oid_array batch = OID_ARRAY_INIT;
	struct rev_info revs;
	struct path_walk_info info = PATH_WALK_INFO_INIT;
	int ret;

	repo_init_revisions(the_repository, &revs, "");
	handle_revision_arg("HEAD", &revs, 0, 0);
	handle_revision_arg("--topo-order", &revs, 0, 0);

	info.revs = &revs;
	info.path_fn = fill_missing_blobs;
	info.path_fn_data = &batch;

	walk_objects_by_path(&info);

	ret = download_batch(&batch);
	oid_array_clear(&batch);
	return ret;
}

int cmd_backfill(int argc, const char **argv, const char *prefix)
{
	struct option options[] = {
		OPT_END(),
	};

	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage_with_options(builtin_backfill_usage, options);

	argc = parse_options(argc, argv, prefix, options, builtin_backfill_usage,
			     0);

	git_config(git_default_config, NULL);

	gh_client__init_block_size(batch_size * 4);

	return do_backfill();
}
