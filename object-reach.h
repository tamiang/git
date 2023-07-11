#ifndef OBJECT_REACH_H
#define OBJECT_REACH_H

#include "commit.h"
#include "commit-slab.h"

struct oid_array;

/*
 * Check if the given 'commit' can reach any one of the objects in the
 * 'objects' oid_array. Instead of using a contains_cache, this method
 * will mark objects that it walks and leave those flags in place until
 * clear_commit_contains_object_flags() is called. This prevents rework
 * in between calls for multiple starting points ('commit').
 *
 * Returns 1 if at least one object in 'objects' is found, returns 0
 * otherwise.
 */
int commit_contains_object(struct repository *r, struct commit *commit,
			   struct oid_array *objects);

void clear_commit_contains_object_flags(struct repository *r);

#endif
