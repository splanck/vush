#!/bin/sh
set -e
failed=0
for test in *.expect; do
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
