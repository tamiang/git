#include "git-compat-util.h"
#include "builtin.h"
#include "service/jobs.h"

int cmd_jobs(int argc, const char **argv, const char *prefix)
{
	return setup_and_run_job_loop();
}