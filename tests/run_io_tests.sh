#!/bin/sh
set -e

if ! command -v expect >/dev/null; then
    echo "Expect is not installed. Please install Expect to run tests." >&2
    exit 1
fi

if [ ! -x ../build/vush ]; then
    echo "Error: ../build/vush not found. Please build the project first." >&2
    exit 1
fi

TMP_HOME=$(mktemp -d)
export HOME="$TMP_HOME"
export VUSH_FUNCFILE=/dev/null
trap 'rm -rf "$TMP_HOME"' EXIT

failed=0

# Input/output related tests
tests="
    test_pipe.expect
    test_redir.expect
    test_assign_redir.expect
    test_err_redir.expect
    test_fd_dup.expect
    test_heredoc.expect
    test_herestring.expect
    test_heredoc_unterminated.expect
    test_read.expect
    test_read_eof.expect
    test_read_signal.expect
    test_process_sub.expect
    test_pipefail.expect
    test_readonly.expect
    test_heredoc_dash.expect
    test_heredoc_tabs.expect
    test_readonly_p.expect
    test_pipe_cr.expect
"

for test in $tests; do
    echo "Running $test"
    if [ "$test" = "test_glob.expect" ]; then
        tmpdir=$(mktemp -d)
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
