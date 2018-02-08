#!/bin/sh

test_description='commit graph'
. ./test-lib.sh

test_expect_success 'setup full repo' '
	rm -rf .git &&
	mkdir full &&
	cd full &&
	git init &&
	git config core.commitGraph true &&
	objdir=".git/objects"
'

test_expect_success 'write graph with no packs' '
	git commit-graph write --object-dir . &&
	test_path_is_missing info/graph-latest
'

test_expect_success 'create commits and repack' '
	for i in $(test_seq 3)
	do
		test_commit $i &&
		git branch commits/$i
	done &&
	git repack
'

graph_git_two_modes() {
	git -c core.graph=true $1 >output
	git -c core.graph=false $1 >expect
	test_cmp output expect
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

graph_git_behavior 'no graph' commits/3 commits/1

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
	test_path_is_missing $objdir/info/graph-latest &&
	git commit-graph read --file=$graph1 >output &&
	graph_read_expect "3" &&
	test_cmp expect output
'

graph_git_behavior 'graph exists, no head' commits/3 commits/1

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
	graph2=$(git commit-graph write --set-latest)&&
	test_path_is_file $objdir/info/$graph2 &&
	test_path_is_file $objdir/info/graph-latest &&
	printf $graph2 >expect &&
	test_cmp expect $objdir/info/graph-latest &&
	git commit-graph read --file=$graph2 >output &&
	graph_read_expect "10" "large_edges" &&
	test_cmp expect output
'

graph_git_behavior 'merge 1 vs 2' merge/1 merge/2
graph_git_behavior 'merge 1 vs 3' merge/1 merge/3
graph_git_behavior 'merge 2 vs 3' merge/2 merge/3

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

graph_git_behavior 'mixed mode, commit 8 vs merge 1' commits/8 merge/1
graph_git_behavior 'mixed mode, commit 8 vs merge 2' commits/8 merge/2

test_expect_success 'write graph with new commit' '
	graph3=$(git commit-graph write --set-latest --delete-expired) &&
	test_path_is_file $objdir/info/$graph3 &&
	test_path_is_file $objdir/info/$graph2 &&
	test_path_is_missing $objdir/info/$graph1 &&
	test_path_is_file $objdir/info/graph-latest &&
	printf $graph3 >expect &&
	test_cmp expect $objdir/info/graph-latest &&
	git commit-graph read --file=$graph3 >output &&
	graph_read_expect "11" "large_edges" &&
	test_cmp expect output
'

graph_git_behavior 'full graph, commit 8 vs merge 1' commits/8 merge/1
graph_git_behavior 'full graph, commit 8 vs merge 2' commits/8 merge/2

test_expect_success 'write graph with nothing new' '
	graph4=$(git commit-graph write --set-latest --delete-expired) &&
	test_path_is_file $objdir/info/$graph4 &&
	test_path_is_missing $objdir/info/$graph2 &&
	printf $graph3 >expect &&
	printf $graph4 >output &&
	test_cmp expect output &&
	test_path_is_file $objdir/info/graph-latest &&
	printf $graph4 >expect &&
	test_cmp expect $objdir/info/graph-latest &&
	git commit-graph read --file=$graph4 >output &&
	graph_read_expect "11" "large_edges" &&
	test_cmp expect output
'

graph_git_behavior 'cleared graph, commit 8 vs merge 1' commits/8 merge/1
graph_git_behavior 'cleared graph, commit 8 vs merge 2' commits/8 merge/2

test_expect_success 'build graph from latest pack with closure' '
	rm $objdir/info/graph-latest &&
	graph5=$(cat new-idx | git commit-graph write --set-latest --delete-expired --stdin-packs) &&
	test_path_is_file $objdir/info/$graph5 &&
	test_path_is_missing $objdir/info/$graph4 &&
	test_path_is_file $objdir/info/graph-latest &&
	printf $graph5 >expect &&
	test_cmp expect $objdir/info/graph-latest &&
	git commit-graph read --file=$graph5 >output &&
	graph_read_expect "9" "large_edges" &&
	test_cmp expect output
'

graph_git_behavior 'graph from pack, commit 8 vs merge 1' commits/8 merge/1
graph_git_behavior 'graph from pack, commit 8 vs merge 2' commits/8 merge/2

test_expect_success 'build graph from commits with closure' '
	git tag -a -m "merge" tag/merge merge/2 &&
	git rev-parse tag/merge >commits-in &&
	git rev-parse merge/1 >>commits-in &&
	rm $objdir/info/graph-latest &&
	graph6=$(cat commits-in | git commit-graph write --set-latest --delete-expired --stdin-commits) &&
	test_path_is_file $objdir/info/$graph6 &&
	test_path_is_missing $objdir/info/$graph5 &&
	test_path_is_file $objdir/info/graph-latest &&
	printf $graph6 >expect &&
	test_cmp expect $objdir/info/graph-latest &&
	git commit-graph read --file=$graph6 >output &&
	graph_read_expect "6" &&
	test_cmp expect output
'

graph_git_behavior 'graph from commits, commit 8 vs merge 1' commits/8 merge/1
graph_git_behavior 'graph from commits, commit 8 vs merge 2' commits/8 merge/2

test_expect_success 'setup bare repo' '
	cd .. &&
	git clone --bare --no-local full bare &&
	cd bare &&
	git config core.commitGraph true &&
	baredir="./objects"
'

graph_git_behavior 'bare repo, commit 8 vs merge 1' commits/8 merge/1
graph_git_behavior 'bare repo, commit 8 vs merge 2' commits/8 merge/2

test_expect_success 'write graph in bare repo' '
	graphbare=$(git commit-graph write --set-latest) &&
	test_path_is_file $baredir/info/$graphbare &&
	test_path_is_file $baredir/info/graph-latest &&
	printf $graphbare >expect &&
	test_cmp expect $baredir/info/graph-latest &&
	git commit-graph read --file=$graphbare >output &&
	graph_read_expect "11" "large_edges" &&
	test_cmp expect output
'

graph_git_behavior 'bare repo with graph, commit 8 vs merge 1' commits/8 merge/1
graph_git_behavior 'bare repo with graph, commit 8 vs merge 2' commits/8 merge/2

test_done

