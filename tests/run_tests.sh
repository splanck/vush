#!/bin/sh
set -e
failed=0
tests="test_env.expect test_pwd.expect test_export.expect test_script.expect test_comments.expect"
for test in $tests; do
    echo "Running $test"
    if ! ./$test; then
        echo "FAILED: $test"
        failed=1
    else
        echo "PASSED: $test"
    fi
    echo
done
exit $failed
