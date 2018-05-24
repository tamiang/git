#!/bin/sh

test_description='meta-pack indexes'
. ./test-lib.sh

test_expect_success 'setup' '
	rm -rf .git &&
	git init &&
	git config core.midx true &&
	git config pack.threads 1
'

test_expect_success 'write-midx with no packs' '
	git midx --write --update-head --delete-expired --pack-dir .
'

test_expect_success 'create packs' '
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
	git update-ref HEAD $commit
'

test_expect_success 'Verify normal git operations succeed' '
	git rev-list --all --objects >rev-list-out-1 &&
	test_line_count = 18 rev-list-out-1
'

test_expect_success 'write-midx from index version 1' '
	pack1=$(git pack-objects --index-version=1 test-1 <obj-list) &&
	midx1=$(git midx --write --pack-dir .) &&
	test -f midx-$midx1.midx &&
	! test -f midx-head &&
	git midx --read --pack-dir . --midx-id=$midx1 >midx-read-out-1 &&
	echo "header: 4d494458 80000001 01 14 00 05 00000001" >midx-read-expect-1 &&
	echo "num_objects: 17" >>midx-read-expect-1 &&
	echo "chunks: pack_lookup pack_names oid_fanout oid_lookup object_offsets" >>midx-read-expect-1 &&
	echo "pack_names:" >>midx-read-expect-1 &&
	echo "test-1-$pack1.pack" >>midx-read-expect-1 &&
	echo "pack_dir: ." >>midx-read-expect-1 &&
	test_cmp midx-read-out-1 midx-read-expect-1
'

test_expect_success 'Verify normal git operations succeed' '
	git rev-list --all --objects >rev-list-out-2 &&
	test_line_count = 18 rev-list-out-2
'

test_expect_success 'write-midx from index version 2' '
	rm "test-1-$pack1.pack" &&
	pack2=$(git pack-objects --index-version=2 test-2 <obj-list) &&
	midx2=$(git midx --write --update-head --pack-dir .) &&
	test -f midx-$midx2.midx &&
	test -f midx-head &&
	printf $midx2 >midx-head-expect &&
	test_cmp midx-head midx-head-expect &&
	git midx --read --pack-dir . --midx-id=$midx2 >midx-read-out-2 &&
	echo "header: 4d494458 80000001 01 14 00 05 00000001" >midx-read-expect-2 &&
	echo "num_objects: 17" >>midx-read-expect-2 &&
	echo "chunks: pack_lookup pack_names oid_fanout oid_lookup object_offsets" >>midx-read-expect-2 &&
	echo "pack_names:" >>midx-read-expect-2 &&
	echo "test-2-$pack2.pack" >>midx-read-expect-2 &&
	echo "pack_dir: ." >>midx-read-expect-2 &&
	test_cmp midx-read-out-2 midx-read-expect-2
'

test_expect_success 'Verify normal git operations succeed' '
	git rev-list --all --objects >rev-list-out-3 &&
	test_line_count = 18 rev-list-out-3
'

test_expect_success 'Add more objects' '
	i=6 &&
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
	pack3=$(git pack-objects --index-version=2 test-pack <obj-list)
'

test_expect_success 'Verify normal git operations succeed in mixed mode' '
	git rev-list --all --objects >rev-list-out-4 &&
	test_line_count = 35 rev-list-out-4
'

test_expect_success 'write-midx with two packs' '
	midx3=$(git midx --write --update-head --delete-expired --pack-dir .) &&
	test -f midx-$midx3.midx &&
	! test -f midx-$midx2.midx &&
	printf $midx3 > midx-head-expect &&
	test_cmp midx-head midx-head-expect &&
	git midx --read --pack-dir . --midx-id=$midx3 >midx-read-out-3 &&
	echo "header: 4d494458 80000001 01 14 00 05 00000002" >midx-read-expect-3 &&
	echo "num_objects: 33" >>midx-read-expect-3 &&
	echo "chunks: pack_lookup pack_names oid_fanout oid_lookup object_offsets" >>midx-read-expect-3 &&
	echo "pack_names:" >>midx-read-expect-3 &&
	echo "test-2-$pack2.pack" >>midx-read-expect-3 &&
	echo "test-pack-$pack3.pack" >>midx-read-expect-3 &&
	echo "pack_dir: ." >>midx-read-expect-3 &&
	test_cmp midx-read-out-3 midx-read-expect-3 &&
	git midx --read --pack-dir . >midx-read-out-3-head &&
	test_cmp midx-read-out-3-head midx-read-expect-3
'

test_expect_success 'Verify normal git operations succeed' '
	git rev-list --all --objects >rev-list-out-5 &&
	test_line_count = 35 rev-list-out-5
'

test_expect_success 'Add more packs' '
	j=0 &&
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
	done
'

test_expect_success 'Verify normal git operations succeed in mixed mode' '
	git rev-list --all --objects >rev-list-out-6 &&
	test_line_count = 90 rev-list-out-6
'

test_expect_success 'write-midx with twelve packs' '
	midx4=$(git midx --write --update-head --delete-expired --pack-dir .) &&
	test -f midx-$midx4.midx &&
	! test -f midx-$midx3.midx &&
	printf $midx4 > midx-head-expect &&
	test_cmp midx-head midx-head-expect &&
	git midx --read --pack-dir . --midx-id=$midx4 >midx-read-out-4 &&
	echo "header: 4d494458 80000001 01 14 00 05 0000000d" >midx-read-expect-4 &&
	echo "num_objects: 77" >>midx-read-expect-4 &&
	echo "chunks: pack_lookup pack_names oid_fanout oid_lookup object_offsets" >>midx-read-expect-4 &&
	echo "pack_names:" >>midx-read-expect-4 &&
	ls test-*.pack | sort >>midx-read-expect-4 &&
	echo "pack_dir: ." >>midx-read-expect-4 &&
	test_cmp midx-read-out-4 midx-read-expect-4 &&
	git midx --read --pack-dir . >midx-read-out-4-head &&
	test_cmp midx-read-out-4-head midx-read-expect-4
'

test_expect_success 'Verify normal git operations succeed' '
	git rev-list --all --objects >rev-list-out-7 &&
	test_line_count = 90 rev-list-out-7
'

test_expect_success 'write-midx with nothing new' '
	midx5=$(git midx --write --update-head --delete-expired --pack-dir .) &&
	printf $midx5 > midx-head-5 &&
	test_cmp midx-head-5 midx-head-expect
'

test_expect_success 'midx --clear' '
	git midx --clear --pack-dir . &&
	! test -f "midx-$midx4.midx" &&
	! test -f "midx-head"
'

test_expect_success 'midx --verify fails on missing midx' '
	test_must_fail git midx --verify --pack-dir .
'

test_expect_success 'Verify normal git operations succeed' '
	git rev-list --all --objects >rev-list-out-8 &&
	test_line_count = 90 rev-list-out-8
'

# usage: corrupt_data <file> <pos> [<data>]
corrupt_data() {
	file=$1
	pos=$2
	data="${3:-\0}"
	printf "$data" | dd of="$file" bs=1 seek="$pos" conv=notrunc
}

# Force 64-bit offsets by manipulating the idx file.
# This makes the IDX file _incorrect_ so be careful to clean up after!
test_expect_success 'force some 64-bit offsets with pack-objects' '
	pack64=$(git pack-objects --index-version=2,0x40 test-64 <obj-list) &&
	idx64=$(ls test-64-*idx) &&
	chmod u+w $idx64 &&
	mkdir packs-64 &&
	mv test-64* packs-64/ &&
	corrupt_data packs-64/$idx64 2863 "\02" &&
	midx64=$(git midx --write --pack-dir packs-64) &&
	git midx --read --pack-dir packs-64 --midx-id=$midx64 >midx-read-out-64 &&
	echo "header: 4d494458 80000001 01 14 00 06 00000001" >midx-read-expect-64 &&
	echo "num_objects: 65" >>midx-read-expect-64 &&
	echo "chunks: pack_lookup pack_names oid_fanout oid_lookup object_offsets large_offsets" >>midx-read-expect-64 &&
	echo "pack_names:" >>midx-read-expect-64 &&
	echo test-64-$pack64.pack >>midx-read-expect-64 &&
	echo "pack_dir: packs-64" >>midx-read-expect-64 &&
	test_cmp midx-read-out-64 midx-read-expect-64 &&
	rm -rf packs-64
'

# The 'verify' commands below expect a midx-head file pointint
# to an existing MIDX file.
test_expect_success 'recompute valid midx' '
	git midx --write --update-head --pack-dir .
'

MIDX_BYTE_VERSION=4
MIDX_BYTE_OID_VERSION=8
MIDX_BYTE_OID_LEN=9
MIDX_BYTE_CHUNK_COUNT=11
MIDX_OFFSET_CHUNK_LOOKUP=16
MIDX_WIDTH_CHUNK_LOOKUP=12
MIDX_BYTE_CHUNK_FANOUT_ID=$MIDX_OFFSET_CHUNK_LOOKUP
MIDX_BYTE_CHUNK_LOOKUP_ID=`expr $MIDX_OFFSET_CHUNK_LOOKUP + \
				1 \* $MIDX_WIDTH_CHUNK_LOOKUP`
MIDX_BYTE_CHUNK_OFFSET_ID=`expr $MIDX_OFFSET_CHUNK_LOOKUP + \
				2 \* $MIDX_WIDTH_CHUNK_LOOKUP`
MIDX_BYTE_CHUNK_PACKLOOKUP_ID=`expr $MIDX_OFFSET_CHUNK_LOOKUP + \
				3 \* $MIDX_WIDTH_CHUNK_LOOKUP`
MIDX_BYTE_CHUNK_PACKNAME_ID=`expr $MIDX_OFFSET_CHUNK_LOOKUP + \
				4 \* $MIDX_WIDTH_CHUNK_LOOKUP`

test_expect_success 'midx --verify succeeds' '
	git midx --verify --pack-dir .
'

# usage: corrupt_midx_and_verify <pos> <data> <string>
corrupt_midx_and_verify() {
	pos=$1
	data="${2:-\0}"
	midxid=$(cat midx-head) &&
	file=midx-$midxid.midx &&
	chmod a+w $file &&
	test_when_finished mv midx-backup $file &&
	cp $file midx-backup &&
	printf "$data" | dd of="$file" bs=1 seek="$pos" conv=notrunc &&
	test_must_fail git midx --verify --pack-dir . 2>test_err &&
	grep -v "^+" test_err >err &&
	grep "$grepstr" err
}

test_expect_success 'verify bad signature' '
	corrupt_midx_and_verify 0 "\00" \
		"midx signature"
'

test_expect_success 'verify bad version' '
	corrupt_midx_and_verify $MIDX_BYTE_VERSION "\02" \
		"midx version"
'

test_expect_success 'verify bad object id version' '
	corrupt_midx_and_verify $MIDX_BYTE_OID_VERSION "\02" \
		"hash version"
'

test_expect_success 'verify bad object id length' '
	corrupt_midx_and_verify $MIDX_BYTE_OID_LEN "\010" \
		"hash length"
'

test_expect_success 'verify bad chunk count' '
	corrupt_midx_and_verify $MIDX_BYTE_CHUNK_COUNT "\01" \
		"missing Packfile Name chunk"
'

test_expect_success 'verify bad OID fanout chunk id' '
	corrupt_midx_and_verify $MIDX_BYTE_CHUNK_FANOUT_ID "\00" \
		"missing OID Fanout chunk"
'

test_expect_success 'verify bad OID lookup chunk id' '
	corrupt_midx_and_verify $MIDX_BYTE_CHUNK_LOOKUP_ID "\00" \
		"missing OID Lookup chunk"
'

test_expect_success 'verify bad offset chunk id' '
	corrupt_midx_and_verify $MIDX_BYTE_CHUNK_OFFSET_ID "\00" \
		"missing Object Offset chunk"
'

test_expect_success 'verify bad packfile lookup chunk id' '
	corrupt_midx_and_verify $MIDX_BYTE_CHUNK_PACKLOOKUP_ID "\00" \
		"missing Packfile Name Lookup chunk"
'

test_expect_success 'verify bad packfile name chunk id' '
	corrupt_midx_and_verify $MIDX_BYTE_CHUNK_PACKNAME_ID "\00" \
		"missing Packfile Name chunk"
'

test_done
