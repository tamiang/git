#!/bin/sh

test_description='git log for a path with bloom filters'
. ./test-lib.sh

test_expect_success 'setup repo' '
	git init &&
	git config core.commitGraph true &&
	git config gc.writeCommitGraph false &&
	infodir=".git/objects/info" &&
	graphdir="$infodir/commit-graphs" &&
	test_oid_init
'

test_expect_success 'create 9 commits and repack' '
	test_commit c1 file1 &&
	test_commit c2 file2 &&
	test_commit c3 file3 &&
	test_commit c4 file1 &&
	test_commit c5 file2 &&
	test_commit c6 file3 &&
	test_commit c7 file1 &&
	test_commit c8 file2 &&
	test_commit c9 file3
'

printf "c7\nc4\nc1" > expect_file1

test_expect_success 'log without bloom filters' '
	git log --pretty="format:%s"  -- file1 > actual &&
	test_cmp expect_file1 actual
'

printf "c8\nc7\nc5\nc4\nc2\nc1" > expect_file1_file2

test_expect_success 'multi-path log without bloom filters' '
	git log --pretty="format:%s"  -- file1 file2 > actual &&
	test_cmp expect_file1_file2 actual
'

graph_read_expect() {
	OPTIONAL=""
	NUM_CHUNKS=5
	if test ! -z $2
	then
		OPTIONAL=" $2"
		NUM_CHUNKS=$((3 + $(echo "$2" | wc -w)))
	fi
	cat >expect <<- EOF
	header: 43475048 1 1 $NUM_CHUNKS 0
	num_commits: $1
	chunks: oid_fanout oid_lookup commit_metadata bloom_indexes bloom_data$OPTIONAL
	EOF
	test-tool read-graph >output &&
	test_cmp expect output
}

test_expect_success 'write commit graph with bloom filters' '
	git commit-graph write --reachable --changed-paths &&
	test_path_is_file $infodir/commit-graph &&
	graph_read_expect "9"
'

test_expect_success 'log using bloom filters' '
	git log --pretty="format:%s" -- file1 > actual &&
	test_cmp expect_file1 actual
'

test_expect_success 'multi-path log using bloom filters' '
	git log --pretty="format:%s"  -- file1 file2 > actual &&
	test_cmp expect_file1_file2 actual
'

test_done
