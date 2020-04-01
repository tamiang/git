#include "git-compat-util.h"
#include "gettext.h"
#include "jobs.h"
#include "config.h"
#include "run-command.h"
#include "strbuf.h"
#include "string-list.h"

int run_loose_objects_job(const char *repo)
{
	fprintf(stderr, "LOOSE_OBJECTS on %s\n", repo);
	return 0;
}
