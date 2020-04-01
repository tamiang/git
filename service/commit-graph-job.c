#include "git-compat-util.h"
#include "gettext.h"
#include "jobs.h"
#include "config.h"
#include "run-command.h"
#include "strbuf.h"
#include "string-list.h"

int run_commit_graph_job(const char *repo)
{
	fprintf(stderr, "COMMIT_GRAPH on %s\n", repo);
	return 0;
}
