#!/bin/sh

test_description='Tests pack performance using bitmaps'
. ./perf-lib.sh

GIT_TEST_PASSING_SANITIZE_LEAK=0
export GIT_TEST_PASSING_SANITIZE_LEAK

test_perf_large_repo

test_expect_success 'create rev input' '
	cat >in-thin <<-EOF &&
	$(git rev-parse HEAD)
	^$(git rev-parse HEAD~1)
	EOF

	cat >in-big-recent <<-EOF
	$(git rev-parse HEAD)
	^$(git rev-parse HEAD~1000)
	EOF
'

test_perf 'thin pack' '
	git pack-objects --thin --stdout --revs --sparse  <in-thin >out
'

test_perf 'thin pack with --full-name-hash' '
	git pack-objects --thin --stdout --revs --sparse--full-name-hash <in-thin >out
'

test_perf 'thin pack with --path-walk' '
	git pack-objects --thin --stdout --revs --sparse --path-walk <in-thin >out
'

test_size 'thin pack' '
	git pack-objects --thin --stdout --revs --sparse  <in-thin >out &&
	wc -c <out
'

test_size 'thin pack with --full-name-hash' '
	git pack-objects --thin --stdout --revs --sparse --full-name-hash <in-thin >out &&
	wc -c <out
'

test_size 'thin pack with --path-walk' '
	git pack-objects --thin --stdout --revs --sparse --path-walk <in-thin >out &&
	wc -c <out
'

test_perf 'full pack' '
	git pack-objects --stdout --revs <in-big-recent >out
'

test_perf 'big recent pack with --full-name-hash' '
	git pack-objects --stdout --revs --full-name-hash <in-big-recent >out
'

test_perf 'big recent pack with --path-walk' '
	git pack-objects --stdout --revs --path-walk <in-big-recent >out
'

test_size 'big recent pack' '
	git pack-objects --stdout --revs  <in-big recent >out &&
	wc -c <out
'

test_size 'big recent pack with --full-name-hash' '
	git pack-objects --stdout --revs --full-name-hash <in-big-recent >out &&
	wc -c <out
'

test_size 'big recent pack with --path-walk' '
	git pack-objects --stdout --revs --path-walk <in-big-recent >out &&
	wc -c <out
'

test_done
