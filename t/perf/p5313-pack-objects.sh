#!/bin/sh

test_description='Tests pack performance using bitmaps'
. ./perf-lib.sh

GIT_TEST_PASSING_SANITIZE_LEAK=0
export GIT_TEST_PASSING_SANITIZE_LEAK

test_perf_large_repo

test_expect_success 'create rev input' '
	first=1 &&
	>in &&
	for parent in $(git rev-list -1 --parents HEAD)
	do
		if test $first -ne 1; then
			printf "^" >>in
		fi &&
		first=0 &&
		echo $parent >>in || return 1
	done
'

test_perf 'thin pack' '
	git pack-objects --thin --stdout --revs --sparse  <in >out
'

test_perf 'thin pack with --path-walk' '
	git pack-objects --thin --stdout --revs --sparse --path-walk <in >out
'

test_size 'thin pack' '
	git pack-objects --thin --stdout --revs --sparse  <in >out &&
	wc -c <out
'

test_size 'thin pack with --path-walk' '
	git pack-objects --thin --stdout --revs --sparse --path-walk <in >out &&
	wc -c <out
'

test_done
