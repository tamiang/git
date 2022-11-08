# Functional testing framework.  Each functional test script starts much
# like a normal test script, except it sources this library instead of
# test-lib.sh.  See t/functional/README for documentation.

# These variables must be set before the inclusion of test-lib.sh below,
# because it will change our working directory.
TEST_DIRECTORY=$(pwd)/..
TEST_OUTPUT_DIRECTORY=$(pwd)

TEST_NO_CREATE_REPO=t
TEST_NO_MALLOC_CHECK=t

. ../test-lib.sh

# Run a 'scalar clone' against a real repository running in Azure DevOps.
# Repository will be located at 'repo/src'.
test_scalar_clone_real_repo () {
	test_expect_success 'scalar clone a real repository' '
		SCALAR_TEST_SKIP_VSTS_INFO=1 \
			scalar clone https://dev.azure.com/gvfs/ci/_git/ForTests repo
	'
}

# usage: test_scalar_repo <name> <test-script>
#
# Run a test that is expected to succeed, but place the script in a subshell
# that is in the previously-cloned repository.
test_scalar_repo () {
	test_expect_success "$1" "
		(
			cd repo/src &&
			$2
		)
	"
}
