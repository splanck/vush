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

tests="
test_export_ps1.expect
test_export.expect
test_unset.expect
test_unset_function.expect
test_local.expect
test_func_scope.expect
test_local_shadow.expect
test_assign_redir.expect
test_var_brace.expect
test_param_expand.expect
test_param_inline.expect
test_set_options.expect
test_nounset.expect
test_for_shellvar.expect
test_assign.expect
test_param_replace.expect
test_param_substring.expect
test_param_indirect.expect
test_param_error.expect
test_readonly.expect
test_pid_params.expect
test_export_p.expect
test_export_n.expect
test_export_p_listing.expect
test_export_quote.expect
test_export_n_unexport.expect
test_readonly_p.expect
test_set_list.expect
test_field_split_module.expect
test_param_expand_module.expect
test_quote_utils_module.expect
test_param_at_q.expect
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
