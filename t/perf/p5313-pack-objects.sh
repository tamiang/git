#!/bin/sh

test_description='Tests pack performance using bitmaps'
. ./perf-lib.sh

GIT_TEST_PASSING_SANITIZE_LEAK=0
export GIT_TEST_PASSING_SANITIZE_LEAK

test_perf_large_repo

test_size 'thin packs for HEAD and no parents' '
	first=1 &&
	>in &&
	for parent in $(git rev-list -1 --parents HEAD)
	do
		if test $first -ne 1; then
			printf "^" >>in
		fi &&
		first=0 &&
		echo $parent >>in || return 1
	done &&

	git pack-objects --thin --stdout --revs <in >out &&
	wc -c <out
'

test_done
