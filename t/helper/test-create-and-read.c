#include "test-tool.h"
#include "git-compat-util.h"
#include <utime.h>

int cmd__create_and_read(int argc, const char **argv)
{
	DIR *dir;
	struct dirent *de;

	if (strcmp(argv[0], "--nfc"))
		creat("\303\244", 0766);
	else if (strcmp(argv[0], "--nfd"))
		creat("\141\314\210", 0766);
	else
		die("select --nfc or --nfd");

	dir = opendir(".");
	readdir(dir);

	while ((de = readdir(dir)) != NULL)
		printf("%s\n", de->d_name);

	return 0;
}
