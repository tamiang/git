#!/bin/sh

test_description="Tests performance of reading the index"

. ./perf-lib.sh

test_perf_default_repo

test_expect_success 'create test data' '
	count=100000 &&
	rm -rf input &&
	for i in $(test_seq 1 $count)
	do
		test-tool genrandom $i $(($i + 100)) >>input
		printf "\n" >>input
	done
'

test_perf 'pkt-line pack' '
	test-tool pkt-line pack <input >packed
'

test_perf 'pkt-line unpack' '
	test-tool pkt-line unpack <packed >output
'

test_done
