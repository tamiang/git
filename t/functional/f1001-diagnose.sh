#!/bin/sh

test_description='check scalar diagnose on a real repository'
. ./func-test-lib.sh

test_scalar_clone_real_repo

test_scalar_repo 'diagnose basics' '
	GIT_TRACE2_PERF="$(pwd)/trace" scalar diagnose 2>err &&
	cat err
'

test_done
