#define USE_THE_REPOSITORY_VARIABLE

#include "test-tool.h"
#include "dir.h"
#include "environment.h"
#include "hex.h"
#include "object-name.h"
#include "object.h"
#include "pretty.h"
#include "revision.h"
#include "setup.h"
#include "strbuf.h"
#include "path-walk.h"
#include "oid-array.h"

struct path_walk_test_data {
	uint32_t commit_nr;
	uint32_t tree_nr;
	uint32_t blob_nr;
	uint32_t tag_nr;
};

static int emit_block(const char *path, struct oid_array *oids,
		      enum object_type type, void *data)
{
	struct path_walk_test_data *tdata = data;
	const char *typestr;

	switch (type) {
	case OBJ_COMMIT:
		typestr = "COMMIT";
		tdata->commit_nr += oids->nr;
		break;

	case OBJ_TREE:
		typestr = "TREE";
		tdata->tree_nr += oids->nr;
		break;

	case OBJ_BLOB:
		typestr = "BLOB";
		tdata->blob_nr += oids->nr;
		break;

	case OBJ_TAG:
		typestr = "TAG";
		tdata->tag_nr += oids->nr;
		break;

	default:
		BUG("we do not understand this type");
	}

	for (size_t i = 0; i < oids->nr; i++) {
		struct object *o = lookup_unknown_object(the_repository,
							 &oids->oid[i]);
		printf("%s:%s:%s", typestr, path, oid_to_hex(&oids->oid[i]));

		if (o->flags & UNINTERESTING)
			printf(":UNINTERESTING");
		printf("\n");
	}

	return 0;
}

int cmd__path_walk(int argc, const char **argv)
{
	int argi, res, stdin_pl = 0;
	struct rev_info revs = REV_INFO_INIT;
	struct path_walk_info info = PATH_WALK_INFO_INIT;
	struct path_walk_test_data data = { 0 };

	initialize_repository(the_repository);
	setup_git_directory();
	revs.repo = the_repository;

	for (argi = 0; argi < argc; argi++) {
		if (!strcmp(argv[argi], "--no-blobs"))
			info.blobs = 0;
		if (!strcmp(argv[argi], "--no-trees"))
			info.trees = 0;
		if (!strcmp(argv[argi], "--no-commits"))
			info.commits = 0;
		if (!strcmp(argv[argi], "--no-tags"))
			info.tags = 0;
		if (!strcmp(argv[argi], "--prune"))
			info.prune_all_uninteresting = 1;
		if (!strcmp(argv[argi], "--stdin-pl"))
			stdin_pl = 1;
		if (!strcmp(argv[argi], "--"))
			break;
	}

	if (argi < argc)
		setup_revisions(argc - argi, argv + argi, &revs, NULL);
	else
		die("usage: test-tool path-walk <options> -- <rev opts>");

	info.revs = &revs;
	info.path_fn = emit_block;
	info.path_fn_data = &data;

	if (stdin_pl) {
		struct strbuf in = STRBUF_INIT;
		CALLOC_ARRAY(info.pl, 1);

		info.pl->use_cone_patterns = 1;

		strbuf_fread(&in, 2048, stdin);
		add_patterns_from_buffer(in.buf, in.len, "", 0, info.pl);
		strbuf_release(&in);
	}

	res = walk_objects_by_path(&info);

	printf("commits:%d\ntrees:%d\nblobs:%d\ntags:%d\n",
	       data.commit_nr, data.tree_nr, data.blob_nr, data.tag_nr);

	if (info.pl) {
		clear_pattern_list(info.pl);
		free(info.pl);
	}
	return res;
}
