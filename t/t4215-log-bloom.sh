#!/bin/sh

test_description='git log for a path with bloom filters'
. ./test-lib.sh

GIT_TEST_COMMIT_GRAPH_BLOOM_FILTERS=0

test_expect_success 'setup the full repo' '
	mkdir full &&
	cd "$TRASH_DIRECTORY/full" &&
	git init &&
	git config core.commitGraph true &&
	objdir=".git/objects" &&
	test_oid_init
'

test_expect_success 'create 9 commits and repack' '
	cd "$TRASH_DIRECTORY/full" &&
	
	echo file1 >> file1 &&
	git add . &&
	test_tick &&
	git commit -m c1 && 
	
	echo file2 >> file2 &&
	git add . &&
	test_tick &&
	git commit -m c2 &&
	
	echo file3 >> file3 &&
	git add . &&
	test_tick &&
	git commit -m c3 &&
	
	echo file1 >> file1 &&
	git add . &&
	test_tick &&
	git commit -m c4 && 
	
	echo file2 >> file2 &&
	git add . &&
	test_tick &&
	git commit -m c5 &&
	
	echo file3 >> file3 &&
	git add . &&
	test_tick &&
	git commit -m c6 &&
	
	echo file1 >> file1 &&
	git add . &&
	test_tick &&
	git commit -m c7 && 
	
	echo file2 >> file2 &&
	git add . &&
	test_tick &&
	git commit -m c8 &&
	
	echo file3 >> file3 &&
	git add . &&
	test_tick &&
	git commit -m c9 &&
	
	git repack
'

printf "c7\nc4\nc1" > expect

test_expect_success 'log without bloom filters' '
	cd "$TRASH_DIRECTORY/full" &&
	git log --pretty="format:%s"  -- file1 > actual &&
	test_cmp expect actual
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
	git commit-graph read >output &&
	test_cmp expect output
}

test_expect_success 'write commit graph with bloom filters' '
	cd "$TRASH_DIRECTORY/full" &&
	git commit-graph write --bloom &&
	test_path_is_file $objdir/info/commit-graph &&
	graph_read_expect "9"
'

printf "c7\nc4\nc1" > expect

test_expect_success 'log using bloom filters' '
	cd "$TRASH_DIRECTORY/full" &&
	git log --pretty="format:%s"  -- file1 > actual &&
	test_cmp expect actual
'

test_done