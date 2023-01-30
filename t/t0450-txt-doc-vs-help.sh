#!/bin/sh

test_description='assert (unbuilt) Documentation/*.txt and -h output

Run this with --debug to see a summary of where we still fail to make
the two versions consistent with one another.'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'setup: list of commands' '
	for builtin in $(git --list-cmds=builtins)
	do
		printf "git %s\n" "$builtin" || return 1
	done >cmds &&

	# Exceptional commands
	cat >>cmds <<-EOF
	scalar
	EOF
'

test_expect_success 'list of txt and help mismatches is sorted' '
	sort -u "$TEST_DIRECTORY"/t0450/txt-help-mismatches >expect &&
	if ! test_cmp expect "$TEST_DIRECTORY"/t0450/txt-help-mismatches
	then
		BUG "please keep the list of txt and help mismatches sorted"
	fi
'

help_to_synopsis () {
	cmd="$1" &&
	out_dir="out/$cmd" &&
	out="$out_dir/help.synopsis" &&
	if test -f "$out"
	then
		echo "$out" &&
		return 0
	fi &&
	mkdir -p "$out_dir" &&
	test_expect_code 129 $cmd -h >"$out.raw" 2>&1 &&
	sed -n \
		-e '1,/^$/ {
			/^$/d;
			s/^usage: //;
			s/^ *or: //;
			p;
		}' <"$out.raw" >"$out" &&
	echo "$out"
}

cmd_to_dashed() {
	printf "$1" | sed s/\ /-/g
}

cmd_to_underscore() {
	printf "$1" | sed s/\ /_/g
}

cmd_to_txt () {
	cmd=$(cmd_to_dashed "$1") &&
	echo "$GIT_BUILD_DIR/Documentation/$cmd.txt"
}

txt_to_synopsis () {
	cmd="$1" &&
	out_dir="out/$cmd" &&
	out="$out_dir/txt.synopsis" &&
	if test -f "$out"
	then
		echo "$out" &&
		return 0
	fi &&
	b2t="$(cmd_to_txt "$cmd")" &&
	sed -n \
		-e '/^\[verse\]$/,/^$/ {
			/^$/d;
			/^\[verse\]$/d;

			s/{litdd}/--/g;
			s/'\''\(git[ a-z-]*\)'\''/\1/g;

			p;
		}' \
		<"$b2t" >"$out" &&
	echo "$out"
}

check_dashed_labels () {
	! grep -E "<[^>_-]+_" "$1"
}

HT="	"

align_after_nl () {
	cmd="$1" &&
	len=$(printf "$cmd" | wc -c) &&
	pad=$(printf "%${len}s" "") &&

	sed "s/^[ $HT][ $HT]*/$pad/"
}

test_debug '>failing'
while read cmd
do
	# -h output assertions
	test_expect_success "$cmd -h output has no \t" '
		h2s="$(help_to_synopsis "$cmd")" &&
		! grep "$HT" "$h2s"
	'

	test_expect_success "$cmd -h output has dashed labels" '
		check_dashed_labels "$(help_to_synopsis "$cmd")"
	'

	test_expect_success "$cmd -h output has consistent spacing" '
		h2s="$(help_to_synopsis "$cmd")" &&
		sed -n \
			-e "/^ / {
				s/[^ ].*//;
				p;
			}" \
			<"$h2s" >help &&
		sort -u help >help.ws &&
		if test -s help.ws
		then
			test_line_count = 1 help.ws
		fi
	'

	txt="$(cmd_to_txt "$cmd")" &&
	underscored="$(cmd_to_dashed "$cmd")" &&
	preq="$(echo BUILTIN_TXT_$underscored | tr '[:lower:]-' '[:upper:]_')" &&

	if test -f "$txt"
	then
		test_set_prereq "$preq"
	fi &&

	# *.txt output assertions
	test_expect_success "$preq" "$cmd *.txt SYNOPSIS has dashed labels" '
		check_dashed_labels "$(txt_to_synopsis "$cmd")"
	'

	# *.txt output consistency assertions
	result=
	if grep -q "^$cmd$" "$TEST_DIRECTORY"/t0450/txt-help-mismatches
	then
		result=failure
	else
		result=success
	fi &&
	test_expect_$result "$preq" "$cmd -h output and SYNOPSIS agree" '
		t2s="$(txt_to_synopsis "$cmd")" &&
		if test "$cmd" = "git merge-tree"
		then
			test_when_finished "rm -f t2s.new" &&
			sed -e '\''s/ (deprecated)$//g'\'' <"$t2s" >t2s.new
			t2s=t2s.new
		fi &&
		h2s="$(help_to_synopsis "$cmd")" &&

		# The *.txt and -h use different spacing for the
		# alignment of continued usage output, normalize it.
		align_after_nl "$cmd" <"$t2s" >txt &&
		align_after_nl "$cmd" <"$h2s" >help &&
		test_cmp txt help
	'

	if test_have_prereq "$preq" && test -e txt && test -e help
	then
		test_debug '
			if test_cmp txt help >cmp 2>/dev/null
			then
				echo "=== DONE: $cmd ==="
			else
				echo "=== TODO: $cmd ===" &&
				cat cmp
			fi >>failing
		'

		# Not in test_expect_success in case --run is being
		# used with --debug
		rm -f txt help tmp 2>/dev/null
	fi
done <cmds

test_debug 'say "$(cat failing)"'

test_done
