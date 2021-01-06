#!/bin/sh

test_description='compare behavior among full checkout, sparse-checkout, sparse-index'

GIT_TEST_CHECK_CACHE_TREE=0
GIT_TEST_SPLIT_INDEX=0

. ./test-lib.sh

test_expect_success 'setup' '
	git init initial-repo &&
	(
		cd initial-repo &&
		echo "initial" >a &&
		mkdir folder1 folder2 deep &&
		mkdir deep/deeper1 deep/deeper2 &&
		mkdir deep/deeper1/deepest &&
		cp a folder1 &&
		cp a folder2 &&
		cp a deep &&
		cp a deep/deeper1 &&
		cp a deep/deeper2 &&
		cp a deep/deeper1/deepest &&
		git add . &&
		git commit -m "initial commit"
	)
'

init_repos () {
	rm -rf full-checkout sparse-checkout sparse-index &&

	# create repos in initial state
	cp -r initial-repo full-checkout &&
	cp -r initial-repo sparse-checkout &&
	git -C sparse-checkout sparse-checkout init --cone &&
	cp -r initial-repo sparse-index &&
	GIT_TEST_SPARSE_INDEX=1 git -C sparse-index sparse-checkout init --cone &&

	# initialize sparse-checkout definitions
	git -C sparse-checkout sparse-checkout set deep &&
	GIT_TEST_SPARSE_INDEX=1 git -C sparse-index sparse-checkout set deep
}

run_on_all () {
	(
		cd full-checkout &&
		$* >../full-checkout-out 2>../full-checkout-err
	) &&
	(
		cd sparse-checkout &&
		$* >../sparse-checkout-out 2>../sparse-checkout-err
	) &&
	(
		cd sparse-index &&
		GIT_TEST_SPARSE_INDEX=1 $* >../sparse-index-out 2>../sparse-index-err
	)
}

test_all_match () {
	run_on_all "$*" &&
	test_cmp full-checkout-out sparse-checkout-out &&
	test_cmp full-checkout-out sparse-index-out &&
	test_cmp full-checkout-err sparse-checkout-err &&
	test_cmp full-checkout-err sparse-index-err
}

test_sparse_match () {
	(
		cd sparse-checkout &&
		$* >../sparse-checkout-out 2>../sparse-checkout-err
	) &&
	(
		cd sparse-index &&
		GIT_TEST_SPARSE_INDEX=1 $* >../sparse-index-out 2>../sparse-index-err
	) &&
	test_cmp sparse-checkout-out sparse-index-out &&
	test_cmp sparse-checkout-err sparse-index-err
}

test_expect_success 'status with options' '
	init_repos &&
	test_sparse_match ls &&
	test_sparse_match git status &&
	test_sparse_match git status -z -u &&
	test_sparse_match git status -uno &&
	run_on_all "touch README.md" &&
	test_sparse_match git status &&
	test_sparse_match git status -z -u &&
	test_sparse_match git status -uno &&
	test_all_match git add README.md &&
	test_sparse_match git status &&
	test_sparse_match git status -z -u &&
	test_sparse_match git status -uno
'

test_expect_success 'add, commit, checkout' '
	init_repos &&

	write_script edit-contents <<-\EOF &&
	echo text >>README.md
	EOF
	run_on_all "../edit-contents" &&

	test_all_match git add README.md &&
	test_sparse_match git status &&
	test_all_match git commit -m "Add README.md" &&

	test_all_match git checkout HEAD~1 &&
	test_all_match git checkout - &&

	run_on_all "../edit-contents" &&

	export GIT_TRACE2_PERF="$(pwd)/add-trace" &&
	test_all_match git add -A &&
	test_sparse_match git status &&
	test_all_match git commit -m "Extend README.md" &&

	test_all_match git checkout HEAD~1 &&
	test_all_match git checkout -
'

test_done
