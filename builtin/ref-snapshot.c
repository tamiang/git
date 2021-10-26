#include "builtin.h"
#include "config.h"
#include "lockfile.h"
#include "object-store.h"
#include "repository.h"
#include "refs/snapshots.h"

static const char ref_snapshot_usage[] = N_("git ref-snapshot");

int cmd_ref_snapshot(int argc, const char **argv, const char *prefix)
{
	if (argc > 1 && !strcmp(argv[1], "-h"))
		usage(_(ref_snapshot_usage));

	if (create_ref_snapshot(the_repository) < 0)
		die(_("failed to generate ref snapshot"));

	return 0;
}
