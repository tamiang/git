#ifndef DEPENDENCIES_H
#define DEPENDENCIES_H

#include "cache.h"
#include "repository.h"
#include "hashmap.h"
#include "string-list.h"

struct dir_hashmap_entry {
	struct hashmap_entry ent;
	char *dir;
};

int dep_hashmap_cmp(const void *unused_cmp_data,
		    const struct hashmap_entry *a,
		    const struct hashmap_entry *b,
		    const void *key);

int fill_dependencies(struct index_state *istate,
		      struct string_list *dirs,
		      struct hashmap *deps);

#endif
