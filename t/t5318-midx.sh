#!/bin/sh

test_description='multi-pack indexes'
. ./test-lib.sh

test_expect_success 'config' \
    'rm -rf .git &&
     mkdir full &&
     cd full &&
     git init &&
     git config core.midx true &&
     git config pack.threads 1 &&
     packdir=.git/objects/pack'

test_expect_success 'write-midx with no packs' \
    'midx0=$(git midx --write) &&
     test "a$midx0" = "a"'

test_expect_success 'create objects' \
    'for i in $(test_seq 100)
     do
         echo $i >file-1-$i
     done &&
     git add file-* &&
     test_tick &&
     git commit -m "test data 1" &&
     git branch commit1 HEAD'

test_expect_success 'write-midx from index version 1' \
    'pack1=$(git rev-list --all --objects | git pack-objects --index-version=1 ${packdir}/test-1) &&
     midx1=$(git midx --write) &&
     test_path_is_file ${packdir}/midx-${midx1}.midx &&
     test_path_is_missing ${packdir}/midx-head'

test_expect_success 'write-midx from index version 2' \
    'rm "${packdir}/test-1-${pack1}.pack" &&
     pack2=$(git rev-list --all --objects | git pack-objects --index-version=2 ${packdir}/test-2) &&
     midx2=$(git midx --write --update-head) &&
     test_path_is_file ${packdir}/midx-${midx2}.midx &&
     test_path_is_file ${packdir}/midx-head &&
     test $(cat ${packdir}/midx-head) = "$midx2"'

test_expect_success 'Create more objects' \
    'for i in $(test_seq 100)
     do
         echo $i >file-2-$i
     done &&
     git add file-* &&
     test_tick &&
     git commit -m "test data 2" &&
     git branch commit2 HEAD'

test_expect_success 'write-midx with two packs' \
    'pack3=$(git rev-list --objects commit2 ^commit1 | git pack-objects --index-version=2 ${packdir}/test-3) &&
     midx3=$(git midx --write --update-head) &&
     test_path_is_file ${packdir}/midx-${midx3}.midx &&
     test_path_is_file ${packdir}/midx-head &&
     test $(cat ${packdir}/midx-head) = "$midx3"'

test_expect_success 'Add more packs' \
    'for j in $(test_seq 10)
     do
         jjj=$(printf '%03i' $j)
         test-genrandom "bar" 200 > wide_delta_$jjj &&
         test-genrandom "baz $jjj" 50 >> wide_delta_$jjj &&
         test-genrandom "foo"$j 100 > deep_delta_$jjj &&
         test-genrandom "foo"$(expr $j + 1) 100 >> deep_delta_$jjj &&
         test-genrandom "foo"$(expr $j + 2) 100 >> deep_delta_$jjj &&
         echo $jjj >file_$jjj &&
         test-genrandom "$jjj" 8192 >>file_$jjj &&
         git update-index --add file_$jjj deep_delta_$jjj wide_delta_$jjj &&
         { echo 101 && test-genrandom 100 8192; } >file_101 &&
         git update-index --add file_101 &&
         commit=$(git commit-tree $EMPTY_TREE -p HEAD</dev/null) && {
         echo $EMPTY_TREE &&
         git ls-tree $EMPTY_TREE | sed -e "s/.* \\([0-9a-f]*\\)	.*/\\1/"
         } >obj-list &&
         echo commit_packs_$j = $commit &&
	 git branch commit_packs_$j $commit &&
         git update-ref HEAD $commit &&
         git pack-objects --index-version=2 ${packdir}/test-pack <obj-list
     done'

test_expect_success 'write-midx with twelve packs' \
    'midx4=$(git midx --write --update-head) &&
     test_path_is_file ${packdir}/midx-${midx4}.midx &&
     test_path_is_file ${packdir}/midx-head &&
     test $(cat ${packdir}/midx-head) = "$midx4"'

test_expect_success 'write-midx with no new packs' \
    'midx5=$(git midx --write --update-head) &&
     test_path_is_file ${packdir}/midx-${midx5}.midx &&
     test "a$midx4" = "a$midx5" &&
     test_path_is_file ${packdir}/midx-head &&
     test $(cat ${packdir}/midx-head) = "$midx4"'

test_expect_success 'create bare repo' \
    'cd .. &&
     git clone --bare full bare &&
     cd bare &&
     git config core.midx true &&
     git config pack.threads 1 &&
     baredir=objects/pack'

test_expect_success 'write-midx in bare repo' \
    'midxbare=$(git midx --write --update-head) &&
     test_path_is_file ${baredir}/midx-${midxbare}.midx  &&
     test_path_is_file ${baredir}/midx-head &&
     test $(cat ${baredir}/midx-head) = "$midxbare"'

test_done
