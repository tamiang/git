#include "cache.h"
#include "packfile.h"

int find_abbrev_loose(const struct object_id *oid,
	const char *path,
	void *data)
{
	find_unique_abbrev(oid->hash, -1);
	return 0;
}

int find_abbrev_packed(const struct object_id *oid,
	struct packed_git *pack,
	uint32_t pos,
	void* data)
{
	find_unique_abbrev(oid->hash, -1);
	return 0;
}

int cmd_main(int ac, const char **av)
{
	setup_git_directory();

	for_each_loose_object(find_abbrev_loose, 0, 0);
	for_each_packed_object(find_abbrev_packed, 0, 0);

	exit(0);
}
