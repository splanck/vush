#!/bin/sh
set -e

if ! command -v expect >/dev/null; then
    echo "Expect is not installed; skipping history tests." >&2
    exit 0
fi

if [ ! -x ../build/vush ]; then
    echo "Error: ../build/vush not found. Please build the project first." >&2
    exit 1
fi

TMP_HOME=$(./mktempd.sh)
export HOME="$TMP_HOME"
export VUSH_FUNCFILE=/dev/null
trap 'rm -rf "$TMP_HOME"' EXIT

failed=0

tests="
test_history.expect
test_history_clear.expect
test_history_limit.expect
test_history_delete.expect
test_lineedit.expect
test_reverse_search.expect
test_forward_search.expect
test_custom_histfile.expect
test_bang_numeric.expect
test_bang_words.expect
"
for test in $tests; do
    echo "Running $test"
    if [ "$test" = "test_glob.expect" ]; then
        tmpdir=$(./mktempd.sh)
        curdir=$(pwd)
        cd "$tmpdir"
        if [ -x "$curdir/$test" ]; then
            cmd="$curdir/$test"
        else
            cmd="expect -f $curdir/$test"
        fi
        if ! eval "$cmd"; then
            echo "FAILED: $test"
            failed=1
        else
            echo "PASSED: $test"
        fi
        cd "$curdir"
        rm -rf "$tmpdir"
        echo
        continue
    fi
    case "$test" in
        test_history.expect|\
        test_history_clear.expect|\
        test_history_limit.expect|\
        test_history_delete.expect|\
        test_bang_*|\
        test_*search.expect|\
        test_lineedit.expect)
            rm -f "$HOME/.vush_history"
            ;;
    esac
    if [ -x "$test" ]; then
        cmd="./$test"
    else
        cmd="expect -f $test"
    fi
    if ! eval "$cmd"; then
        echo "FAILED: $test"
        failed=1
    else
        echo "PASSED: $test"
    fi
    echo
done
exit $failed
