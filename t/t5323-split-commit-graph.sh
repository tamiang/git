#!/bin/sh

test_description='split commit graph'
. ./test-lib.sh

test_expect_success 'setup repo' '
	git init &&
	git config core.commitGraph true &&
	infodir=".git/objects/info" &&
	graphdir="$infodir/commit-graphs" &&
	test_oid_init
'

graph_read_expect() {
	NUM_BASE=0
	if test ! -z $2
	then
		NUM_BASE=$2
	fi
	cat >expect <<- EOF
	header: 43475048 1 1 3 $NUM_BASE
	num_commits: $1
	chunks: oid_fanout oid_lookup commit_metadata
	EOF
	git commit-graph read >output &&
	test_cmp expect output
}

test_expect_success 'create commits and write commit-graph' '
	for i in $(test_seq 3)
	do
		test_commit $i &&
		git branch commits/$i
	done &&
	git commit-graph write --reachable &&
	test_path_is_file $infodir/commit-graph &&
	graph_read_expect 3
'

graph_git_two_modes() {
	git -c core.commitGraph=true $1 >output
	git -c core.commitGraph=false $1 >expect
	test_cmp expect output
}

graph_git_behavior() {
	MSG=$1
	BRANCH=$2
	COMPARE=$3
	test_expect_success "check normal git operations: $MSG" '
		graph_git_two_modes "log --oneline $BRANCH" &&
		graph_git_two_modes "log --topo-order $BRANCH" &&
		graph_git_two_modes "log --graph $COMPARE..$BRANCH" &&
		graph_git_two_modes "branch -vv" &&
		graph_git_two_modes "merge-base -a $BRANCH $COMPARE"
	'
}

graph_git_behavior 'graph exists' commits/3 commits/1

test_expect_success 'add more commits, and write a new base graph' '
	git reset --hard commits/1 &&
	for i in $(test_seq 4 5)
	do
		test_commit $i &&
		git branch commits/$i
	done &&
	git reset --hard commits/2 &&
	for i in $(test_seq 6 7)
	do
		test_commit $i &&
		git branch commits/$i
	done &&
	git reset --hard commits/2 &&
	git merge commits/4 &&
	git branch merge/1 &&
	git reset --hard commits/4 &&
	git merge commits/6 &&
	git branch merge/2 &&
	git commit-graph write --reachable &&
	graph_read_expect 9
'

test_expect_success 'add three more commits, write a tip graph' '
	git reset --hard commits/3 &&
	git merge merge/1 &&
	git merge commits/5 &&
	git merge merge/2 &&
	git branch merge/3 &&
	git commit-graph write --reachable --split &&
	test_path_is_missing $infodir/commit-graph &&
	test_path_is_file $graphdir/commit-graph-chain &&
	ls $graphdir/commit-graph-* >graph-files &&
	test_line_count = 3 graph-files
'

graph_git_behavior 'split commit-graph: merge 3 vs 2' merge/3 merge/2

test_expect_success 'add one commit, write a tip graph' '
	test_commit 10 &&
	git branch commits/10 &&
	git commit-graph write --reachable --split &&
	test_path_is_missing $infodir/commit-graph &&
	test_path_is_file $graphdir/commit-graph-chain &&
	ls $graphdir/commit-graph-* >graph-files &&
	test_line_count = 4 graph-files
'

graph_git_behavior 'three-layer commit-graph: commit 10 vs 6' commits/10 commits/6

test_done
