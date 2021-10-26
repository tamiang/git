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
#include "../object-store.h"
#include "../blob.h"
#include "../repository.h"
#include "../remote.h"
#include "../strbuf.h"
#include "snapshots.h"
#include "../run-command.h"

static void get_snapshot_dir(const struct repository *repo,
			     struct strbuf *buf)
{
	strbuf_reset(buf);
	strbuf_addstr(buf, repo->commondir);
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
	struct child_process cp = CHILD_PROCESS_INIT;

	time_t t = time(NULL);

	get_snapshot_dir(repo, &dirname);

	fprintf(stderr, "dirname: %s\n", dirname.buf);

	safe_create_leading_directories(dirname.buf);
	mkdir(dirname.buf, 0777);

	strbuf_addf(&data, "%"PRIuMAX"\n", t);

	for_each_ref_in("refs/heads/", append_ref_to_snapshot, &data);

	child_process_init(&cp);
	strvec_pushl(&cp.args, "-C", repo->commondir, "hash-object", "-w", "--stdin", NULL);
	strvec_pushf(&cp.env_array, DB_ENVIRONMENT "=%s", dirname.buf);
	cp.in = -1;
	cp.git_cmd = 1;

	if (start_command(&cp))
		return -1;

	write_in_full(cp.in, data.buf, data.len);
	close(cp.in);

	finish_command(&cp);

	strbuf_release(&data);
	strbuf_release(&dirname);
	return result;
}
