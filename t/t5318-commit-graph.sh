#!/bin/sh

test_description='commit graph'
. ./test-lib.sh

test_expect_success 'setup full repo' '
	rm -rf .git &&
	mkdir full &&
	cd full &&
	git init &&
	objdir=".git/objects"
'

test_expect_success 'write graph with no packs' '
	git commit-graph write --object-dir .
'

test_expect_success 'create commits and repack' '
	for i in $(test_seq 3)
	do
		test_commit $i &&
		git branch commits/$i
	done &&
	git repack
'

graph_read_expect() {
	OPTIONAL=""
	NUM_CHUNKS=3
	if [ ! -z $2 ]
	then
		OPTIONAL=" $2"
		NUM_CHUNKS=$((3 + $(echo "$2" | wc -w)))
	fi
	cat >expect <<- EOF
	header: 43475048 1 1 $NUM_CHUNKS 0
	num_commits: $1
	chunks: oid_fanout oid_lookup commit_metadata$OPTIONAL
	EOF
}

test_expect_success 'write graph' '
	graph1=$(git commit-graph write) &&
	test_path_is_file $objdir/info/$graph1 &&
	git commit-graph read --file=$graph1 >output &&
	graph_read_expect "3" &&
	test_cmp expect output
'

test_expect_success 'Add more commits' '
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
	git reset --hard commits/3 &&
	git merge commits/5 commits/7 &&
	git branch merge/3 &&
	git repack
'

# Current graph structure:
#
#   __M3___
#  /   |   \
# 3 M1 5 M2 7
# |/  \|/  \|
# 2    4    6
# |___/____/
# 1


test_expect_success 'write graph with merges' '
	graph2=$(git commit-graph write)&&
	test_path_is_file $objdir/info/$graph2 &&
	git commit-graph read --file=$graph2 >output &&
	graph_read_expect "10" "large_edges" &&
	test_cmp expect output
'

test_expect_success 'Add one more commit' '
	test_commit 8 &&
	git branch commits/8 &&
	ls $objdir/pack | grep idx >existing-idx &&
	git repack &&
	ls $objdir/pack| grep idx | grep -v --file=existing-idx >new-idx
'

# Current graph structure:
#
#      8
#      |
#   __M3___
#  /   |   \
# 3 M1 5 M2 7
# |/  \|/  \|
# 2    4    6
# |___/____/
# 1

test_expect_success 'write graph with new commit' '
	graph3=$(git commit-graph write) &&
	test_path_is_file $objdir/info/$graph3 &&
	git commit-graph read --file=$graph3 >output &&
	graph_read_expect "11" "large_edges" &&
	test_cmp expect output
'

test_expect_success 'write graph with nothing new' '
	graph4=$(git commit-graph write) &&
	test_path_is_file $objdir/info/$graph4 &&
	printf $graph3 >expect &&
	printf $graph4 >output &&
	test_cmp expect output &&
	git commit-graph read --file=$graph4 >output &&
	graph_read_expect "11" "large_edges" &&
	test_cmp expect output
'

test_expect_success 'setup bare repo' '
	cd .. &&
	git clone --bare --no-local full bare &&
	cd bare &&
	baredir="./objects"
'

test_expect_success 'write graph in bare repo' '
	graphbare=$(git commit-graph write) &&
	test_path_is_file $baredir/info/$graphbare &&
	git commit-graph read --file=$graphbare >output &&
	graph_read_expect "11" "large_edges" &&
	test_cmp expect output
'

test_done

