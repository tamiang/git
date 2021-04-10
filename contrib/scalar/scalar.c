/*
 * This is a port of Scalar to C.
 */

#include "git-compat-util.h"
#include "strbuf.h"
#include "strvec.h"
#include "run-command.h"

int cmd_main(int argc, const char **argv)
{
	struct strbuf buf = STRBUF_INIT;
	struct child_process cp = CHILD_PROCESS_INIT;

	strvec_pushl(&cp.args, "version", "--build-options", NULL);
	cp.git_cmd = 1;

	if (pipe_command(&cp, NULL, 0, &buf, 0, &buf, 0) < 0)
		die("could not execute `git version`");
	else
		printf("`git version` said this:\n%s\n", buf.buf);
	strbuf_release(&buf);

	return 0;
}
