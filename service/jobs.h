#ifndef JOBS_H__
#define JOBS_H__

#include "git-compat-util.h"

enum job_id {
	NONE = 0,
	COMMIT_GRAPH,
	FETCH,
	LOOSE_OBJECTS,
	MULTI_PACK_INDEX,

	/* leave last, for limiting job list size */
	MAX_JOB_COUNT
};

struct job_description {
	enum job_id id;
};

int setup_and_run_job_loop(void);

#endif
