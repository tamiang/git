#include "cache.h"
#include "config.h"
#include "dir.h"
#include "hashmap.h"
#include "quote.h"
#include "repository.h"
#include "sparse-checkout.h"
#include "strbuf.h"
#include "string-list.h"
#include "unpack-trees.h"
#include "object-store.h"
#include "dependencies.h"

int dep_hashmap_cmp(const void *unused_cmp_data,
		    const struct hashmap_entry *a,
		    const struct hashmap_entry *b,
		    const void *key)
{
	const struct dir_hashmap_entry *ee1 =
			container_of(a, struct dir_hashmap_entry, ent);
	const struct dir_hashmap_entry *ee2 =
			container_of(b, struct dir_hashmap_entry, ent);

	return strcmp(ee1->dir, ee2->dir);
}

static int fill_dependencies_one(struct index_state *istate,
				 const char *dir,
				 struct hashmap *deps)
{
	int result = 0;
	struct object_id *oid;
	enum object_type type;
	char *buf = NULL;
	char *cur, *end;
	unsigned long size;
	int pos;
	struct strbuf dep_file = STRBUF_INIT;
	struct dir_hashmap_entry *entry;

	if (!strlen(dir))
		return 0;
	
	entry = xmalloc(sizeof(*entry));
	entry->dir = xstrdup(dir);

	hashmap_entry_init(&entry->ent,
			   strhash(entry->dir));
	
	/* Have we already visited this directory? */
	if (hashmap_get_entry(deps, entry, ent, NULL)) {
		free(entry);
		goto cleanup;
	}

	hashmap_add(deps, &entry->ent);

	if (dir[0] == '/')
		strbuf_addstr(&dep_file, dir + 1);
	else
		strbuf_addstr(&dep_file, dir);

	strbuf_trim_trailing_dir_sep(&dep_file);
	strbuf_addstr(&dep_file, "/.gitdependencies");

	pos = index_name_pos(istate, dep_file.buf, dep_file.len);

	/* Not found, but that's fine. */
	if (pos < 0)
		goto cleanup;

	oid = &istate->cache[pos]->oid;
	type = oid_object_info(the_repository, oid, NULL);

	if (type != OBJ_BLOB) {
		warning(_("expected a file at '%s' with oid '%s'; not updating sparse-checkout"),
			dep_file.buf,
			oid_to_hex(oid));
		result = 1;
		goto cleanup;
	}

	buf = read_object_file(oid, &type, &size);
	end = buf + size;

	for (cur = buf; cur < end; ) {
		char *next;

		next = memchr(cur, '\n', end - cur);

		if (next)
			*next = '\0';

		if (next > cur + 1) {
			result = fill_dependencies_one(istate, cur, deps);

			if (result)
				goto cleanup;
		}

		if (next)
			cur = next + 1;
		else
			break;
	}

cleanup:
	free(buf);
	strbuf_release(&dep_file);
	return result;
}

int fill_dependencies(struct index_state *istate,
		      struct string_list *dirs,
		      struct hashmap *deps)
{
	int result = 0;
	struct string_list_item *item;

	for_each_string_list_item(item, dirs) {
		result = fill_dependencies_one(istate, item->string, deps);

		if (result)
			break;
	}

	return result;
}