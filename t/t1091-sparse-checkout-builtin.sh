#!/bin/sh

test_description='sparse checkout builtin tests'

. ./test-lib.sh

test_expect_success 'setup' '
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
	git commit -m "initial commit" &&
	mkdir test-output
'

test_expect_success 'git sparse-checkout list (empty)' '
	git sparse-checkout list >test-output/list &&
	test_line_count = 0 test-output/list
'

test_expect_success 'git sparse-checkout init' '
	git sparse-checkout init &&
	cat >test-output/expect <<-EOF &&
		/*
		!/*/*
	EOF
	test_cmp test-output/expect .git/info/sparse-checkout &&
	git config --list >test-output/config &&
	test_i18ngrep "core.sparsecheckout=true" test-output/config &&
	ls >test-output/dir  &&
	cat >test-output/expect <<-EOF &&
		a
		test-output
	EOF
	test_cmp test-output/expect test-output/dir
'

test_expect_success 'git sparse-checkout list after init' '
	git sparse-checkout list >test-output/actual &&
	echo "/" >test-output/expect &&
	test_cmp test-output/expect test-output/actual
'

test_expect_success 'git sparse-checkout add' '
	git sparse-checkout add </dev/null &&
	git sparse-checkout list >test-output/actual &&
	test_cmp test-output/expect test-output/actual &&
	git sparse-checkout add <<-EOF &&
		folder1
		deep/deeper1
	EOF
	cat >test-output/expect <<-EOF &&
		/*
		!/*/*
		/deep/*
		!/deep/*/*
		/deep/deeper1/*
		/folder1/*
	EOF
	test_cmp test-output/expect .git/info/sparse-checkout &&
	ls >test-output/dir &&
	cat >test-output/expect <<-EOF &&
		a
		deep
		folder1
		test-output
	EOF
	test_cmp test-output/expect test-output/dir &&
	ls deep >test-output/dir &&
	cat >test-output/expect <<-EOF &&
		a
		deeper1
	EOF
	test_cmp test-output/expect test-output/dir
'

test_expect_success 'git sparse-checkout list after add' '
	git sparse-checkout list >test-output/actual &&
	cat >test-output/expect <<-EOF &&
		/
		/deep/
		/deep/deeper1/*
		/folder1/*
	EOF
	test_cmp test-output/expect test-output/actual
'

test_expect_success 'git sparse-checkout add more' '
	git sparse-checkout add <<-EOF &&
		folder1
		deep/deeper2
	EOF
	cat >test-output/expect <<-EOF &&
		/*
		!/*/*
		/deep/*
		!/deep/*/*
		/deep/deeper1/*
		/deep/deeper2/*
		/folder1/*
	EOF
	test_cmp test-output/expect .git/info/sparse-checkout &&
	ls >test-output/dir &&
	cat >test-output/expect <<-EOF &&
		a
		deep
		folder1
		test-output
	EOF
	test_cmp test-output/expect test-output/dir &&
	ls deep >test-output/dir &&
	cat >test-output/expect <<-EOF &&
		a
		deeper1
		deeper2
	EOF
	test_cmp test-output/expect test-output/dir
'


test_done