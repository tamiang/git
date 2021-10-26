#include "../cache.h"
#include "../config.h"
#include "../refs.h"
#include "refs-internal.h"
#include "ref-cache.h"
#include "packed-backend.h"
#include "../iterator.h"
#include "../dir-iterator.h"
#include "../lockfile.h"
#include "../object.h"
#include "../repository.h"
#include "../remote.h"
#include "../strbuf.h"
#include "snapshots.h"

static void get_snapshot_dir(const struct repository *repo,
			     struct strbuf *buf)
{
	strbuf_reset(buf);
	strbuf_addstr(buf, repo->gitdir);
	strbuf_addstr(buf, "/snapshots/self/");
}

static int append_ref_to_snapshot(const char *refname,
				  const struct object_id *oid,
				  int flags, void *cb_data)
{
	struct strbuf *buf = cb_data;
	strbuf_addf(buf, "%s %s\n", oid_to_hex(oid), refname);
	return 0;
}

int create_ref_snapshot(const struct repository *repo)
{
	int result = 0;
	struct strbuf dirname = STRBUF_INIT;
	struct strbuf data = STRBUF_INIT;
	time_t t = time(NULL);

	get_snapshot_dir(repo, &dirname);
	safe_create_leading_directories(dirname.buf);

	strbuf_addf(&data, "%"PRIuMAX"\n", t);

	for_each_ref_in("refs/heads/", append_ref_to_snapshot, &data);

	fprintf(stdout, "%s\n", data.buf);

	strbuf_release(&data);
	strbuf_release(&dirname);
	return result;
}
