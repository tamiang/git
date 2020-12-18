#include "test-tool.h"
#include "cache.h"
#include "config.h"

static void print_cache_entry(struct cache_entry *ce)
{
	/* stat info */
	printf("%08x %08x %08x %08x %08x %08x ",
	       ce->ce_stat_data.sd_ctime.sec,
	       ce->ce_stat_data.sd_ctime.nsec,
	       ce->ce_stat_data.sd_mtime.sec,
	       ce->ce_stat_data.sd_mtime.nsec,
	       ce->ce_stat_data.sd_dev,
	       ce->ce_stat_data.sd_ino);

	/* mode in binary */
	printf("0b%d%d%d%d ",
		(ce->ce_mode >> 15) & 1,
		(ce->ce_mode >> 14) & 1,
		(ce->ce_mode >> 13) & 1,
		(ce->ce_mode >> 12) & 1);

	/* output permissions? */
	printf("%04o ", ce->ce_mode & 01777);

	printf("%s ", oid_to_hex(&ce->oid));

	printf("%s\n", ce->name);
}

static void print_cache(struct index_state *cache)
{
	int i;
	for (i = 0; i < the_index.cache_nr; i++)
		print_cache_entry(the_index.cache[i]);
}

int cmd__read_cache(int argc, const char **argv)
{
	int i, cnt = 1;
	const char *name = NULL;
	int table = 0;

	if (argc > 1 && skip_prefix(argv[1], "--print-and-refresh=", &name)) {
		argc--;
		argv++;
	}
	if (argc > 1 && !strcmp("--table", argv[1])) {
		table = 1;
		argc--;
		argv++;
	}

	if (argc == 2)
		cnt = strtol(argv[1], NULL, 0);
	setup_git_directory();
	git_config(git_default_config, NULL);
	for (i = 0; i < cnt; i++) {
		read_cache();
		if (name) {
			int pos;

			refresh_index(&the_index, REFRESH_QUIET,
				      NULL, NULL, NULL);
			pos = index_name_pos(&the_index, name, strlen(name));
			if (pos < 0)
				die("%s not in index", name);
			printf("%s is%s up to date\n", name,
			       ce_uptodate(the_index.cache[pos]) ? "" : " not");
			write_file(name, "%d\n", i);
		}
		if (table)
			print_cache(&the_index);
		discard_cache();
	}
	return 0;
}
