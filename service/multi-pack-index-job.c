#include "git-compat-util.h"
#include "gettext.h"
#include "jobs.h"
#include "config.h"
#include "run-command.h"
#include "strbuf.h"
#include "string-list.h"

int run_multi_pack_index_job(const char *repo)
{
	fprintf(stderr, "MULTI_PACK_INDEX on %s\n", repo);
	return 0;
}