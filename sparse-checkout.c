#include "cache.h"
#include "config.h"
#include "dir.h"
#include "hashmap.h"
#include "repository.h"
#include "run-command.h"
#include "sparse-checkout.h"
#include "strbuf.h"

static int core_sparse_checkout = -1;

int use_sparse_checkout(struct repository *r)
{
	if (core_sparse_checkout >= 0)
		return core_sparse_checkout;

	if (repo_config_get_bool(r, "core.sparsecheckout", &core_sparse_checkout))
		core_sparse_checkout = 0;

	return core_sparse_checkout;
}

int get_sparse_checkout_data(char *sparse_filename,
			     struct exclude_list *el)
{
	el->use_restricted_patterns = 1;

	if (add_excludes_from_file_to_list(sparse_filename, "", 0, el, NULL) < 0)
		return -1;
	return 0;
}

int excludes_are_strict(struct exclude_list *el)
{
	/* TODO: actually implement! */
	return 1;
}