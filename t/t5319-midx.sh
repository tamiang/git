#!/bin/sh

test_description='meta-pack indexes'
. ./test-lib.sh

test_expect_success \
    'setup' \
    'rm -rf .git &&
     git init &&
     git config core.midx true &&
     git config pack.threads 1'

test_expect_success \
    'write-midx with no packs' \
    'git midx --write --update-head --delete-expired --pack-dir .'

test_expect_success \
    'create packs' \
    'i=1 &&
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
    'Verify normal git operations succeed' \
    'git rev-list --all --objects | wc -l >rev-list-out-1 &&
     echo 18 >rev-list-expect-1 &&
     cmp -n 2 rev-list-out-1 rev-list-expect-1'

test_expect_success \
    'write-midx from index version 1' \
    'pack1=$(git pack-objects --index-version=1 test-1 <obj-list) &&
     midx1=$(git midx --write --pack-dir .) &&
     test -f midx-${midx1}.midx &&
     ! test -f midx-head &&
     git midx --read --pack-dir . --midx-id=${midx1} >midx-read-out-1 &&
     echo "header: 4d494458 80000001 01 14 00 05 00000001" >midx-read-expect-1 &&
     echo "num_objects: 17" >>midx-read-expect-1 &&
     echo "chunks: pack_lookup pack_names oid_fanout oid_lookup object_offsets" >>midx-read-expect-1 &&
     echo "pack_names:" >>midx-read-expect-1 &&
     echo "test-1-${pack1}.pack" >>midx-read-expect-1 &&
     echo "pack_dir: ." >>midx-read-expect-1 &&
     cmp midx-read-out-1 midx-read-expect-1'

test_expect_success \
    'Verify normal git operations succeed' \
    'git rev-list --all --objects | wc -l >rev-list-out-2 &&
     echo 18 >rev-list-expect-2 &&
     cmp -n 2 rev-list-out-2 rev-list-expect-2'

test_expect_success \
    'write-midx from index version 2' \
    'rm "test-1-${pack1}.pack" &&
     pack2=$(git pack-objects --index-version=2 test-2 <obj-list) &&
     midx2=$(git midx --write --update-head --pack-dir .) &&
     test -f midx-${midx2}.midx &&
     test -f midx-head &&
     echo ${midx2} >midx-head-expect &&
     cmp -n 40 midx-head midx-head-expect &&
     git midx --read --pack-dir . --midx-id=${midx2} >midx-read-out-2 &&
     echo "header: 4d494458 80000001 01 14 00 05 00000001" >midx-read-expect-2 &&
     echo "num_objects: 17" >>midx-read-expect-2 &&
     echo "chunks: pack_lookup pack_names oid_fanout oid_lookup object_offsets" >>midx-read-expect-2 &&
     echo "pack_names:" >>midx-read-expect-2 &&
     echo "test-2-${pack2}.pack" >>midx-read-expect-2 &&
     echo "pack_dir: ." >>midx-read-expect-2 &&
     cmp midx-read-out-2 midx-read-expect-2'

test_expect_success \
    'Verify normal git operations succeed' \
    'git rev-list --all --objects | wc -l >rev-list-out-3 &&
     echo 18 >rev-list-expect-3 &&
     cmp -n 2 rev-list-out-3 rev-list-expect-3'

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
     git update-ref HEAD $commit &&
     pack3=$(git pack-objects --index-version=2 test-pack <obj-list)'

test_expect_success \
    'Verify normal git operations succeed in mixed mode' \
    'git rev-list --all --objects | wc -l >rev-list-out-4 &&
     echo 35 >rev-list-expect-4 &&
     cmp -n 2 rev-list-out-4 rev-list-expect-4'

test_expect_success \
    'write-midx with two packs' \
    'midx3=$(git midx --write --update-head --delete-expired --pack-dir .) &&
     test -f midx-${midx3}.midx &&
     ! test -f midx-${midx2}.midx &&
     echo ${midx3} > midx-head-expect &&
     cmp -n 40 midx-head midx-head-expect &&
     git midx --read --pack-dir . --midx-id=${midx3} >midx-read-out-3 &&
     echo "header: 4d494458 80000001 01 14 00 05 00000002" >midx-read-expect-3 &&
     echo "num_objects: 33" >>midx-read-expect-3 &&
     echo "chunks: pack_lookup pack_names oid_fanout oid_lookup object_offsets" >>midx-read-expect-3 &&
     echo "pack_names:" >>midx-read-expect-3 &&
     echo "test-2-${pack2}.pack" >>midx-read-expect-3 &&
     echo "test-pack-${pack3}.pack" >>midx-read-expect-3 &&
     echo "pack_dir: ." >>midx-read-expect-3 &&
     cmp midx-read-out-3 midx-read-expect-3 &&
     git midx --read --pack-dir . >midx-read-out-3-head &&
     cmp midx-read-out-3-head midx-read-expect-3'

test_expect_success \
    'Verify normal git operations succeed' \
    'git rev-list --all --objects | wc -l >rev-list-out-5 &&
     echo 35 >rev-list-expect-5 &&
     cmp -n 2 rev-list-out-5 rev-list-expect-5'

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
         git pack-objects --index-version=2 test-pack <obj-list &&
         i=$(expr $i + 1) || return 1 &&
         j=$(expr $j + 1) || return 1
     done'

test_expect_success \
    'Verify normal git operations succeed in mixed mode' \
    'git rev-list --all --objects | wc -l >rev-list-out-6 &&
     echo 90 >rev-list-expect-6 &&
     cmp -n 2 rev-list-out-6 rev-list-expect-6'

test_expect_success \
    'write-midx with twelve packs' \
    'midx4=$(git midx --write --update-head --delete-expired --pack-dir .) &&
     test -f midx-${midx4}.midx &&
     ! test -f midx-${midx3}.midx &&
     echo ${midx4} > midx-head-expect &&
     cmp -n 40 midx-head midx-head-expect &&
     git midx --read --pack-dir . --midx-id=${midx4} >midx-read-out-4 &&
     echo "header: 4d494458 80000001 01 14 00 05 0000000d" >midx-read-expect-4 &&
     echo "num_objects: 77" >>midx-read-expect-4 &&
     echo "chunks: pack_lookup pack_names oid_fanout oid_lookup object_offsets" >>midx-read-expect-4 &&
     echo "pack_names:" >>midx-read-expect-4 &&
     ls test-*.pack | sort >>midx-read-expect-4 &&
     echo "pack_dir: ." >>midx-read-expect-4 &&
     cmp midx-read-out-4 midx-read-expect-4 &&
     git midx --read --pack-dir . >midx-read-out-4-head &&
     cmp midx-read-out-4-head midx-read-expect-4'

test_expect_success \
    'Verify normal git operations succeed' \
    'git rev-list --all --objects | wc -l >rev-list-out-7 &&
     echo 90 >rev-list-expect-7 &&
     cmp -n 2 rev-list-out-7 rev-list-expect-7'

test_expect_success \
    'write-midx with nothing new' \
    'midx5=$(git midx --write --update-head --delete-expired --pack-dir .) &&
     echo ${midx5} > midx-head-5 &&
     cmp -n 40 midx-head-5 midx-head-expect'

test_expect_success \
    'midx --clear' \
    'git midx --clear --pack-dir . &&
     ! test -f "midx-${midx4}.midx" &&
     ! test -f "midx-head"'

test_expect_success \
    'Verify normal git operations succeed' \
    'git rev-list --all --objects | wc -l >rev-list-out-8 &&
     echo 90 >rev-list-expect-8 &&
     cmp -n 2 rev-list-out-8 rev-list-expect-8'

test_done
