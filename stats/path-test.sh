#!/bin/bash

gitdir=$1

rm -r trace2-*-$1.txt

GIT_TRACE2_PERF=$(pwd)/trace2-perf-$1.txt
GIT_TRACE2_EVENT=$(pwd)/trace2-event-$1.txt

export GIT_TRACE2_PERF
export GIT_TRACE2_EVENT

for path in $(cat paths.txt)
do
	/_git/$1/git rev-list HEAD -- "$path" >/dev/null
done

