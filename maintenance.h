#ifndef MAINTENANCE_H__
#define MAINTENANCE_H__

#define MAINTENANCE_QUIET		(1 << 0)
#define MAINTENANCE_REDIRECT_ERROR	(1 << 1)
#define MAINTENANCE_OVERRIDE_CONFIG	(1 << 2)

struct repository;

typedef void (*post_process_func)(struct child_process *);
void post_command_maintenance(struct repository *r, int flags,
			      post_process_func ppf);

#endif
