#include "cache.h"
#include "config.h"
#include "repository.h"

#define UPDATE_DEFAULT_BOOL(s,v) do { if (s == -1) { s = v; } } while(0)

void prepare_repo_settings(struct repository *r)
{
	int value;
	char *strval;

	if (r->settings.initialized)
		return;

	/* Defaults */
	memset(&r->settings, -1, sizeof(r->settings));

	if (!repo_config_get_bool(r, "core.commitgraph", &value))
		r->settings.core_commit_graph = value;
	if (!repo_config_get_bool(r, "gc.writecommitgraph", &value))
		r->settings.gc_write_commit_graph = value;
	UPDATE_DEFAULT_BOOL(r->settings.core_commit_graph, 1);
	UPDATE_DEFAULT_BOOL(r->settings.gc_write_commit_graph, 1);

	if (!repo_config_get_bool(r, "index.version", &value))
		r->settings.index_version = value;
	if (!repo_config_get_maybe_bool(r, "core.untrackedcache", &value)) {
		if (value == 0)
			r->settings.core_untracked_cache = UNTRACKED_CACHE_REMOVE;
		else
			r->settings.core_untracked_cache = UNTRACKED_CACHE_WRITE;
	} else if (!repo_config_get_string(r, "core.untrackedcache", &strval)) {
		if (!strcasecmp(strval, "keep"))
			r->settings.core_untracked_cache = UNTRACKED_CACHE_KEEP;

		free(strval);
	}


	if (!repo_config_get_bool(r, "pack.usesparse", &value))
		r->settings.pack_use_sparse = value;

	/* Hack for test programs like test-dump-untracked-cache */
	if (ignore_untracked_cache_config)
		r->settings.core_untracked_cache = UNTRACKED_CACHE_KEEP;
	else
		UPDATE_DEFAULT_BOOL(r->settings.core_untracked_cache, UNTRACKED_CACHE_KEEP);
}
