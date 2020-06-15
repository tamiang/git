#!/bin/bash

repo=$1

for gitdir in git-bloom git
do
	(
	cd $repo

	echo $repo $gitdir

	../path-test.sh $gitdir
	../trace2-to-table.py $gitdir >../$repo-stats-$gitdir.csv
	../trace2-to-times.py $gitdir >../$repo-times-$gitdir.csv
	)
done

