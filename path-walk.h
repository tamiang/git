/*
 * path-walk.h : Methods and structures for walking the object graph in batches
 * by the paths that can reach those objects.
 */
#include "object.h" /* Required for 'enum object_type'. */

struct rev_info;
struct oid_array;

/**
 * The type of a function pointer for the method that is called on a list of
 * objects reachable at a given path.
 */
typedef int (*path_fn)(const char *path,
		       struct oid_array *oids,
		       enum object_type type,
		       void *data);

struct path_walk_info {
	/**
	 * revs provides the definitions for the commit walk, including
	 * which commits are UNINTERESTING or not.
	 */
	struct rev_info *revs;

	/**
	 * The caller wishes to execute custom logic on objects reachable at a
	 * given path. Every reachable object will be visited exactly once, and
	 * the first path to see an object wins. This may not be a stable choice.
	 */
	path_fn path_fn;
	void *path_fn_data;

	/**
	 * If 'skip_all_uninteresting' is set, then only walk paths that have at
	 * least one object without the UNINTERESTING flag.
	 */
	int skip_all_uninteresting;

	/**
	 * If not NULL, 'path_patterns' must have 'use_cone_patterns' true in
	 * order to restrict the list of chosen paths by a cone-mode sparse-
	 * checkout definition.
	 */
	struct pattern_list *path_patterns;

	/**
	 * If 'progress' is set, then use progress indicators for the user to
	 * know which stage the search is in and how many paths have been
	 * explored/discovered.
	 */
	int progress;
};

#define PATH_WALK_INFO_INIT { 0 }

/**
 * Given the configuration of 'info', walk the commits based on 'info->revs' and
 * call 'info->path_fn' on each discovered path.
 *
 * Returns nonzero on an error.
 */
int walk_objects_by_path(struct path_walk_info *info);
