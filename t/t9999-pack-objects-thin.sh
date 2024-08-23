#!/bin/sh
#

test_description='test delta compression of thin packs'

. ./test-lib.sh

test_expect_success 'setup' '
	git checkout -b left &&
	mkdir a &&
	mkdir b &&

	echo constant >a/middle.txt &&
	echo constant >b/middle.txt &&

	cat >a/left.txt <<-EOF &&
	common: first
	unique: a left
	common: second
	unique: another a left
	common: third
	EOF

	cat >a/right.txt <<-EOF &&
	common: first
	unique: a right
	common: second
	unique: another a right
	common: third
	EOF

	cat >b/left.txt <<-EOF &&
	common firsts
	unique b left
	common second
	unique another b left
	common third
	EOF

	cat >b/right.txt <<-EOF &&
	common first
	unique b right
	common second
	unique another b right
	common third
	EOF

	git add a b &&
	git commit -m "base" &&

	cat >a/left.txt <<-EOF &&
	common: first
	unique: a left modified
	common: second
	unique: another modified left
	common: third
	EOF

	cat >b/left.txt <<-EOF &&
	common: first
	unique: b left modified
	common: second
	unique: another modified b left
	common: third
	EOF

	git add a b &&
	git commit -m "left" &&

	git checkout -b right HEAD~1 &&

	cat >a/right.txt <<-EOF &&
	common: first
	unique: a modified right
	common: second
	unique: another right modified
	common: third
	EOF

	cat >b/right.txt <<-EOF &&
	common: first
	unique: modified b right
	common: second
	unique: another b right modified
	common: third
	EOF

	git add a b &&
	git commit -m "right" &&

	git checkout -b merge left &&
	git merge right -m "merge" &&

	echo newline >>a/left.txt &&
	echo newline >>b/right.txt &&
	git add a b &&
	git commit --amend --no-edit
'

test_expect_success 'test thin pack deltas' '
	cat >in <<-EOF &&
	$(git rev-parse merge)
	^$(git rev-parse merge^1)
	^$(git rev-parse merge^2)
	EOF

	GIT_TRACE2_PERF="$(pwd)/trace.txt" git pack-objects \
		--no-sparse --no-reuse-object --thin --revs --stdout \
		<in >out &&

	cat out | wc
'

test_done
