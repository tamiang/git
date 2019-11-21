#!/bin/sh

test_description='commit graph bloom filters'
. ./test-lib.sh

GIT_TEST_COMMIT_GRAPH_BLOOM_FILTERS=1

test_expect_success 'setup full repo' '
	mkdir full &&
	cd "$TRASH_DIRECTORY/full" &&
	git init &&
	git config core.commitGraph true &&
	objdir=".git/objects" &&
	test_oid_init
'

test_expect_success 'create commits and repack' '
	cd "$TRASH_DIRECTORY/full" &&
	for i in $(test_seq 3)
	do
		test_commit $i &&
		git branch commits/$i
	done &&
	git repack
'

graph_git_behavior 'no graph' full commits/3 commits/1

graph_read_expect() {
	OPTIONAL=""
	NUM_CHUNKS=5
	if test ! -z $2
	then
		OPTIONAL=" $2"
		NUM_CHUNKS=$(($NUM_CHUNKS + $(echo "$2" | wc -w)))
	fi
	cat >expect <<- EOF
	header: 43475048 1 1 $NUM_CHUNKS 0
	num_commits: $1
	chunks: oid_fanout oid_lookup commit_metadata$OPTIONAL bloom_indexes bloom_data
	EOF
	git commit-graph read >output &&
	test_cmp expect output
}

test_expect_success 'write graph' '
	cd "$TRASH_DIRECTORY/full" &&
	git commit-graph write &&
	test_path_is_file $objdir/info/commit-graph &&
	graph_read_expect "3"
'

GIT_TEST_COMMIT_GRAPH_BLOOM_FILTERS=0

test_expect_success 'write graph with --bloom option' '
	cd "$TRASH_DIRECTORY/full" &&
	git commit-graph write --bloom &&
	test_path_is_file $objdir/info/commit-graph &&
	graph_read_expect "3"
'

GIT_TEST_COMMIT_GRAPH_BLOOM_FILTERS=1

graph_git_behavior 'graph exists' full commits/3 commits/1

test_expect_success 'Add more commits' '
	cd "$TRASH_DIRECTORY/full" &&
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

test_expect_success 'commit-graph write progress off for redirected stderr' '
	cd "$TRASH_DIRECTORY/full" &&
	git commit-graph write 2>err &&
	test_line_count = 0 err
'

test_expect_success 'commit-graph write progress off for redirected stderr' '
	cd "$TRASH_DIRECTORY/full" &&
	git commit-graph write 2>err &&
	test_line_count = 0 err
'
test_expect_success 'commit-graph write force progress on for stderr' '
	cd "$TRASH_DIRECTORY/full" &&
	git commit-graph write --progress 2>err &&
	test_file_not_empty err
'

test_expect_success 'commit-graph write with the --no-progress option' '
	cd "$TRASH_DIRECTORY/full" &&
	git commit-graph write --no-progress 2>err &&
	test_line_count = 0 err
'

test_expect_success 'commit-graph verify progress off for redirected stderr' '
	cd "$TRASH_DIRECTORY/full" &&
	git commit-graph verify 2>err &&
	test_line_count = 0 err
'

test_expect_success 'commit-graph verify force progress on for stderr' '
	cd "$TRASH_DIRECTORY/full" &&
	git commit-graph verify --progress 2>err &&
	test_file_not_empty err
'

test_expect_success 'commit-graph verify with the --no-progress option' '
	cd "$TRASH_DIRECTORY/full" &&
	git commit-graph verify --no-progress 2>err &&
	test_line_count = 0 err
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
	cd "$TRASH_DIRECTORY/full" &&
	git commit-graph write &&
	test_path_is_file $objdir/info/commit-graph &&
	graph_read_expect "10" "extra_edges"
'

graph_git_behavior 'merge 1 vs 2' full merge/1 merge/2
graph_git_behavior 'merge 1 vs 3' full merge/1 merge/3
graph_git_behavior 'merge 2 vs 3' full merge/2 merge/3

test_expect_success 'Add one more commit' '
	cd "$TRASH_DIRECTORY/full" &&
	test_commit 8 &&
	git branch commits/8 &&
	ls $objdir/pack | grep idx >existing-idx &&
	git repack &&
	ls $objdir/pack| grep idx | grep -v -f existing-idx >new-idx
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

graph_git_behavior 'mixed mode, commit 8 vs merge 1' full commits/8 merge/1
graph_git_behavior 'mixed mode, commit 8 vs merge 2' full commits/8 merge/2

test_expect_success 'write graph with new commit' '
	cd "$TRASH_DIRECTORY/full" &&
	git commit-graph write &&
	test_path_is_file $objdir/info/commit-graph &&
	graph_read_expect "11" "extra_edges"
'

graph_git_behavior 'full graph, commit 8 vs merge 1' full commits/8 merge/1
graph_git_behavior 'full graph, commit 8 vs merge 2' full commits/8 merge/2

test_expect_success 'write graph with nothing new' '
	cd "$TRASH_DIRECTORY/full" &&
	git commit-graph write &&
	test_path_is_file $objdir/info/commit-graph &&
	graph_read_expect "11" "extra_edges"
'

graph_git_behavior 'cleared graph, commit 8 vs merge 1' full commits/8 merge/1
graph_git_behavior 'cleared graph, commit 8 vs merge 2' full commits/8 merge/2

test_expect_success 'build graph from latest pack with closure' '
	cd "$TRASH_DIRECTORY/full" &&
	cat new-idx | git commit-graph write --stdin-packs &&
	test_path_is_file $objdir/info/commit-graph &&
	graph_read_expect "9" "extra_edges"
'

graph_git_behavior 'graph from pack, commit 8 vs merge 1' full commits/8 merge/1
graph_git_behavior 'graph from pack, commit 8 vs merge 2' full commits/8 merge/2

test_expect_success 'build graph from commits with closure' '
	cd "$TRASH_DIRECTORY/full" &&
	git tag -a -m "merge" tag/merge merge/2 &&
	git rev-parse tag/merge >commits-in &&
	git rev-parse merge/1 >>commits-in &&
	cat commits-in | git commit-graph write --stdin-commits &&
	test_path_is_file $objdir/info/commit-graph &&
	graph_read_expect "6"
'

graph_git_behavior 'graph from commits, commit 8 vs merge 1' full commits/8 merge/1
graph_git_behavior 'graph from commits, commit 8 vs merge 2' full commits/8 merge/2

test_expect_success 'build graph from commits with append' '
	cd "$TRASH_DIRECTORY/full" &&
	git rev-parse merge/3 | git commit-graph write --stdin-commits --append &&
	test_path_is_file $objdir/info/commit-graph &&
	graph_read_expect "10" "extra_edges"
'

graph_git_behavior 'append graph, commit 8 vs merge 1' full commits/8 merge/1
graph_git_behavior 'append graph, commit 8 vs merge 2' full commits/8 merge/2

test_expect_success 'build graph using --reachable' '
	cd "$TRASH_DIRECTORY/full" &&
	git commit-graph write --reachable &&
	test_path_is_file $objdir/info/commit-graph &&
	graph_read_expect "11" "extra_edges"
'

graph_git_behavior 'append graph, commit 8 vs merge 1' full commits/8 merge/1
graph_git_behavior 'append graph, commit 8 vs merge 2' full commits/8 merge/2

test_expect_success 'setup bare repo' '
	cd "$TRASH_DIRECTORY" &&
	git clone --bare --no-local full bare &&
	cd bare &&
	git config core.commitGraph true &&
	baredir="./objects"
'

graph_git_behavior 'bare repo, commit 8 vs merge 1' bare commits/8 merge/1
graph_git_behavior 'bare repo, commit 8 vs merge 2' bare commits/8 merge/2

test_expect_success 'write graph in bare repo' '
	cd "$TRASH_DIRECTORY/bare" &&
	git commit-graph write &&
	test_path_is_file $baredir/info/commit-graph &&
	graph_read_expect "11" "extra_edges"
'

graph_git_behavior 'bare repo with graph, commit 8 vs merge 1' bare commits/8 merge/1
graph_git_behavior 'bare repo with graph, commit 8 vs merge 2' bare commits/8 merge/2

test_expect_success 'perform fast-forward merge in full repo' '
	cd "$TRASH_DIRECTORY/full" &&
	git checkout -b merge-5-to-8 commits/5 &&
	git merge commits/8 &&
	git show-ref -s merge-5-to-8 >output &&
	git show-ref -s commits/8 >expect &&
	test_cmp expect output
'

test_expect_success 'check that gc computes commit-graph - passes' '
	cd "$TRASH_DIRECTORY/full" &&
	git commit --allow-empty -m "blank" &&
	git commit-graph write --reachable &&
	cp $objdir/info/commit-graph commit-graph-before-gc &&
	git reset --hard HEAD~1 &&
	git config gc.writeCommitGraph true &&
	git gc &&
	cp $objdir/info/commit-graph commit-graph-after-gc &&
	! test_cmp_bin commit-graph-before-gc commit-graph-after-gc
'

#test_expect_success 'check that gc computes commit-graph - fails' '
#	cd "$TRASH_DIRECTORY/full" &&
#	git commit --allow-empty -m "blank" &&
#	git commit-graph write --reachable &&
#	cp $objdir/info/commit-graph commit-graph-before-gc &&
#	git reset --hard HEAD~1 &&
#	git config gc.writeCommitGraph true &&
#	git gc &&
#	cp $objdir/info/commit-graph commit-graph-after-gc &&
#	! test_cmp_bin commit-graph-before-gc commit-graph-after-gc &&
#	git commit-graph write --reachable &&
#	test_cmp_bin commit-graph-after-gc $objdir/info/commit-graph
#'

test_expect_success 'replace-objects invalidates commit-graph' '
	cd "$TRASH_DIRECTORY" &&
	test_when_finished rm -rf replace &&
	git clone full replace &&
	(
		cd replace &&
		git commit-graph write --reachable &&
		test_path_is_file .git/objects/info/commit-graph &&
		git replace HEAD~1 HEAD~2 &&
		git -c core.commitGraph=false log >expect &&
		git -c core.commitGraph=true log >actual &&
		test_cmp expect actual &&
		git commit-graph write --reachable &&
		git -c core.commitGraph=false --no-replace-objects log >expect &&
		git -c core.commitGraph=true --no-replace-objects log >actual &&
		test_cmp expect actual &&
		rm -rf .git/objects/info/commit-graph &&
		git commit-graph write --reachable &&
		test_path_is_file .git/objects/info/commit-graph
	)
'

test_expect_success 'commit grafts invalidate commit-graph' '
	cd "$TRASH_DIRECTORY" &&
	test_when_finished rm -rf graft &&
	git clone full graft &&
	(
		cd graft &&
		git commit-graph write --reachable &&
		test_path_is_file .git/objects/info/commit-graph &&
		H1=$(git rev-parse --verify HEAD~1) &&
		H3=$(git rev-parse --verify HEAD~3) &&
		echo "$H1 $H3" >.git/info/grafts &&
		git -c core.commitGraph=false log >expect &&
		git -c core.commitGraph=true log >actual &&
		test_cmp expect actual &&
		git commit-graph write --reachable &&
		git -c core.commitGraph=false --no-replace-objects log >expect &&
		git -c core.commitGraph=true --no-replace-objects log >actual &&
		test_cmp expect actual &&
		rm -rf .git/objects/info/commit-graph &&
		git commit-graph write --reachable &&
		test_path_is_missing .git/objects/info/commit-graph
	)
'

test_expect_success 'replace-objects invalidates commit-graph' '
	cd "$TRASH_DIRECTORY" &&
	test_when_finished rm -rf shallow &&
	git clone --depth 2 "file://$TRASH_DIRECTORY/full" shallow &&
	(
		cd shallow &&
		git commit-graph write --reachable &&
		test_path_is_missing .git/objects/info/commit-graph &&
		git fetch origin --unshallow &&
		git commit-graph write --reachable &&
		test_path_is_file .git/objects/info/commit-graph
	)
'

test_expect_success 'git commit-graph verify' '
	cd "$TRASH_DIRECTORY/full" &&
	git rev-parse commits/8 | git commit-graph write --stdin-commits &&
	git commit-graph verify >output
'

test_expect_success 'setup non-the_repository tests' '
	rm -rf repo &&
	git init repo &&
	test_commit -C repo one &&
	test_commit -C repo two &&
	git -C repo config core.commitGraph true &&
	git -C repo rev-parse two | \
		git -C repo commit-graph write --stdin-commits
'

test_expect_success 'parse_commit_in_graph works for non-the_repository' '
	test-tool repository parse_commit_in_graph \
		repo/.git repo "$(git -C repo rev-parse two)" >actual &&
	{
		git -C repo log --pretty=format:"%ct " -1 &&
		git -C repo rev-parse one
	} >expect &&
	test_cmp expect actual &&

	test-tool repository parse_commit_in_graph \
		repo/.git repo "$(git -C repo rev-parse one)" >actual &&
	git -C repo log --pretty="%ct" -1 one >expect &&
	test_cmp expect actual
'

test_expect_success 'get_commit_tree_in_graph works for non-the_repository' '
	test-tool repository get_commit_tree_in_graph \
		repo/.git repo "$(git -C repo rev-parse two)" >actual &&
	git -C repo rev-parse two^{tree} >expect &&
	test_cmp expect actual &&

	test-tool repository get_commit_tree_in_graph \
		repo/.git repo "$(git -C repo rev-parse one)" >actual &&
	git -C repo rev-parse one^{tree} >expect &&
	test_cmp expect actual
'

test_done
