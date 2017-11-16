#!/bin/sh

test_description='meta-pack indexes'
. ./test-lib.sh

test_expect_success \
    'setup' \
    'rm -rf .git &&
     git init &&
     git config core.midx true &&
     git config pack.threads 1 &&
     i=1 &&
     while test $i -le 5
     do
         iii=$(printf '%03i' $i)
         test-tool genrandom "bar" 200 > wide_delta_$iii &&
         test-tool genrandom "baz $iii" 50 >> wide_delta_$iii &&
         test-tool genrandom "foo"$i 100 > deep_delta_$iii &&
         test-tool genrandom "foo"$(expr $i + 1) 100 >> deep_delta_$iii &&
         test-tool genrandom "foo"$(expr $i + 2) 100 >> deep_delta_$iii &&
         echo $iii >file_$iii &&
         test-tool genrandom "$iii" 8192 >>file_$iii &&
         git update-index --add file_$iii deep_delta_$iii wide_delta_$iii &&
         i=$(expr $i + 1) || return 1
     done &&
     { echo 101 && test-tool genrandom 100 8192; } >file_101 &&
     git update-index --add file_101 &&
     tree=$(git write-tree) &&
     commit=$(git commit-tree $tree </dev/null) && {
	 echo $tree &&
	 git ls-tree $tree | sed -e "s/.* \\([0-9a-f]*\\)	.*/\\1/"
     } >obj-list &&
     git update-ref HEAD $commit'

test_expect_success \
    'write-midx from index version 1' \
    'pack1=$(git pack-objects --index-version=1 test-1 <obj-list) &&
     midx1=$(git midx --write --pack-dir .) &&
     test -f midx-${midx1}.midx'

test_expect_success \
    'write-midx from index version 2' \
    'rm "test-1-${pack1}.pack" &&
     pack2=$(git pack-objects --index-version=2 test-2 <obj-list) &&
     midx2=$(git midx --write --pack-dir .) &&
     test -f midx-${midx2}.midx &&
     ! test -f midx-head'

test_expect_success \
    'Add more objects' \
    'i=6 &&
     while test $i -le 10
     do
         iii=$(printf '%03i' $i)
         test-tool genrandom "bar" 200 > wide_delta_$iii &&
         test-tool genrandom "baz $iii" 50 >> wide_delta_$iii &&
         test-tool genrandom "foo"$i 100 > deep_delta_$iii &&
         test-tool genrandom "foo"$(expr $i + 1) 100 >> deep_delta_$iii &&
         test-tool genrandom "foo"$(expr $i + 2) 100 >> deep_delta_$iii &&
         echo $iii >file_$iii &&
         test-tool genrandom "$iii" 8192 >>file_$iii &&
         git update-index --add file_$iii deep_delta_$iii wide_delta_$iii &&
         i=$(expr $i + 1) || return 1
     done &&
     { echo 101 && test-tool genrandom 100 8192; } >file_101 &&
     git update-index --add file_101 &&
     tree=$(git write-tree) &&
     commit=$(git commit-tree $tree -p HEAD</dev/null) && {
	 echo $tree &&
	 git ls-tree $tree | sed -e "s/.* \\([0-9a-f]*\\)	.*/\\1/"
     } >obj-list &&
     git update-ref HEAD $commit'

test_expect_success \
    'write-midx with two packs' \
    'pack3=$(git pack-objects --index-version=2 test-3 <obj-list) &&
     midx3=$(git midx --write --pack-dir .) &&
     test -f midx-${midx3}.midx'

test_expect_success \
    'Add more packs' \
    'j=0 &&
     while test $j -le 10
     do
         iii=$(printf '%03i' $i)
         test-tool genrandom "bar" 200 > wide_delta_$iii &&
         test-tool genrandom "baz $iii" 50 >> wide_delta_$iii &&
         test-tool genrandom "foo"$i 100 > deep_delta_$iii &&
         test-tool genrandom "foo"$(expr $i + 1) 100 >> deep_delta_$iii &&
         test-tool genrandom "foo"$(expr $i + 2) 100 >> deep_delta_$iii &&
         echo $iii >file_$iii &&
         test-tool genrandom "$iii" 8192 >>file_$iii &&
         git update-index --add file_$iii deep_delta_$iii wide_delta_$iii &&
         { echo 101 && test-tool genrandom 100 8192; } >file_101 &&
         git update-index --add file_101 &&
         tree=$(git write-tree) &&
         commit=$(git commit-tree $tree -p HEAD</dev/null) && {
         echo $tree &&
         git ls-tree $tree | sed -e "s/.* \\([0-9a-f]*\\)	.*/\\1/"
         } >obj-list &&
         git update-ref HEAD $commit &&
         git pack-objects --index-version=2 test-4 <obj-list &&
         i=$(expr $i + 1) || return 1 &&
         j=$(expr $j + 1) || return 1
     done'

test_expect_success \
    'write-midx with twelve packs' \
    'midx4=$(git midx --write --pack-dir .) &&
     test -f midx-${midx4}.midx'

test_done
