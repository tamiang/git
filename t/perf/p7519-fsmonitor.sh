#!/bin/sh

test_description="Test core.fsmonitor"

. ./perf-lib.sh

#
# Performance test for the fsmonitor feature which enables git to talk to a
# file system change monitor and avoid having to scan the working directory
# for new or modified files.
#
# By default, the performance test will utilize the Watchman file system
# monitor if it is installed.  If Watchman is not installed, it will use a
# dummy integration script that does not report any new or modified files.
# The dummy script has very little overhead which provides optimistic results.
#
# The performance test will also use the untracked cache feature if it is
# available as fsmonitor uses it to speed up scanning for untracked files.
#
# There are 3 environment variables that can be used to alter the default
# behavior of the performance test:
#
# GIT_PERF_7519_UNTRACKED_CACHE: used to configure core.untrackedCache
# GIT_PERF_7519_SPLIT_INDEX: used to configure core.splitIndex
# GIT_PERF_7519_FSMONITOR: used to configure core.fsMonitor
#
# The big win for using fsmonitor is the elimination of the need to scan the
# working directory looking for changed and untracked files. If the file
# information is all cached in RAM, the benefits are reduced.
#
# GIT_PERF_7519_DROP_CACHE: if set, the OS caches are dropped between tests
#

test_perf_large_repo
test_checkout_worktree

test_lazy_prereq UNTRACKED_CACHE '
	{ git update-index --test-untracked-cache; ret=$?; } &&
	test $ret -ne 1
'

test_lazy_prereq WATCHMAN '
	command -v watchman
'

if test_have_prereq WATCHMAN
then
	# Convert unix style paths to escaped Windows style paths for Watchman
	case "$(uname -s)" in
	MSYS_NT*)
	  GIT_WORK_TREE="$(cygpath -aw "$PWD" | sed 's,\\,/,g')"
	  ;;
	*)
	  GIT_WORK_TREE="$PWD"
	  ;;
	esac
fi

if test -n "$GIT_PERF_7519_DROP_CACHE"
then
	# When using GIT_PERF_7519_DROP_CACHE, GIT_PERF_REPEAT_COUNT must be 1 to
	# generate valid results. Otherwise the caching that happens for the nth
	# run will negate the validity of the comparisons.
	if test "$GIT_PERF_REPEAT_COUNT" -ne 1
	then
		echo "warning: Setting GIT_PERF_REPEAT_COUNT=1" >&2
		GIT_PERF_REPEAT_COUNT=1
	fi
fi

test_expect_success "one time repo setup" '
	# set untrackedCache depending on the environment
	if test -n "$GIT_PERF_7519_UNTRACKED_CACHE"
	then
		git config core.untrackedCache "$GIT_PERF_7519_UNTRACKED_CACHE"
	else
		if test_have_prereq UNTRACKED_CACHE
		then
			git config core.untrackedCache true
		else
			git config core.untrackedCache false
		fi
	fi &&

	# set core.splitindex depending on the environment
	if test -n "$GIT_PERF_7519_SPLIT_INDEX"
	then
		git config core.splitIndex "$GIT_PERF_7519_SPLIT_INDEX"
	fi &&

	mkdir 1_file 10_files 100_files 1000_files 10000_files &&
	for i in $(test_seq 1 10); do touch 10_files/$i; done &&
	for i in $(test_seq 1 100); do touch 100_files/$i; done &&
	for i in $(test_seq 1 1000); do touch 1000_files/$i; done &&
	for i in $(test_seq 1 10000); do touch 10000_files/$i; done &&
	git add 1_file 10_files 100_files 1000_files 10000_files &&
	git commit -m "Add files" &&

	# If Watchman exists, watch the work tree and attempt a query.
	if test_have_prereq WATCHMAN; then
		watchman watch "$GIT_WORK_TREE" &&
		watchman watch-list | grep -q -F "$GIT_WORK_TREE"
	fi
'

test_expect_success "setup for fsmonitor" '
	# set INTEGRATION_SCRIPT depending on the environment
	if test -n "$GIT_PERF_7519_FSMONITOR"
	then
		INTEGRATION_SCRIPT="$GIT_PERF_7519_FSMONITOR"
	else
		#
		# Choose integration script based on existence of Watchman.
		# Fall back to an empty integration script.
		#
		mkdir .git/hooks &&
		if test_have_prereq WATCHMAN
		then
			INTEGRATION_SCRIPT=".git/hooks/fsmonitor-watchman" &&
			cp "$TEST_DIRECTORY/../templates/hooks--fsmonitor-watchman.sample" "$INTEGRATION_SCRIPT"
		else
			INTEGRATION_SCRIPT=".git/hooks/fsmonitor-empty" &&
			write_script "$INTEGRATION_SCRIPT"<<-\EOF
			EOF
		fi
	fi &&

	git config core.fsmonitor "$INTEGRATION_SCRIPT" &&
	git update-index --fsmonitor &&
	git status  # Warm caches
'

test_perf_w_drop_caches () {
	if test -n "$GIT_PERF_7519_DROP_CACHE"; then
		test-tool drop-caches
	fi

	test_perf "$@"
}

test_fsmonitor_suite() {
	test_perf_w_drop_caches "status (fsmonitor=$INTEGRATION_SCRIPT)" '
		git status
	'

	test_perf_w_drop_caches "status -uno (fsmonitor=$INTEGRATION_SCRIPT)" '
		git status -uno
	'

	test_perf_w_drop_caches "status -uall (fsmonitor=$INTEGRATION_SCRIPT)" '
		git status -uall
	'

	test_perf_w_drop_caches "diff (fsmonitor=$INTEGRATION_SCRIPT)" '
		git diff
	'

	test_perf_w_drop_caches "diff -- 0_files (fsmonitor=$INTEGRATION_SCRIPT)" '
		git diff -- 1_file
	'

	test_perf_w_drop_caches "diff -- 10_files (fsmonitor=$INTEGRATION_SCRIPT)" '
		git diff -- 10_files
	'

	test_perf_w_drop_caches "diff -- 100_files (fsmonitor=$INTEGRATION_SCRIPT)" '
		git diff -- 100_files
	'

	test_perf_w_drop_caches "diff -- 1000_files (fsmonitor=$INTEGRATION_SCRIPT)" '
		git diff -- 1000_files
	'

	test_perf_w_drop_caches "diff -- 10000_files (fsmonitor=$INTEGRATION_SCRIPT)" '
		git diff -- 10000_files
	'

	test_perf_w_drop_caches "add (fsmonitor=$INTEGRATION_SCRIPT)" '
		git add  --all
	'
}

test_fsmonitor_suite

test_expect_success "setup without fsmonitor" '
	unset INTEGRATION_SCRIPT &&
	git config --unset core.fsmonitor &&
	git update-index --no-fsmonitor
'

test_fsmonitor_suite

if test_have_prereq WATCHMAN
then
	watchman watch-del "$GIT_WORK_TREE" >/dev/null 2>&1 &&

	# Work around Watchman bug on Windows where it holds on to handles
	# preventing the removal of the trash directory
	watchman shutdown-server >/dev/null 2>&1
fi

test_done
