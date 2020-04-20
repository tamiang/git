#!/bin/sh

test_description='basic git gc tests
'

. ./test-lib.sh

test_expect_success 'disable post-command step' '
	git init to-gc &&
	GIT_TRACE2_EVENT="$TRASH_DIRECTORY/trace2" git -C to-gc maintenance run &&
	grep "\"git\",\"gc\",\"--auto\"" trace2  &&
	rm -f trace2 &&
	git -C to-gc config jobs.post-command.enabled false &&
	GIT_TRACE2_EVENT="$TRASH_DIRECTORY/trace2" git -C to-gc maintenance run &&
	! grep "\"git\",\"gc\",\"--auto\"" trace2
'

test_done
