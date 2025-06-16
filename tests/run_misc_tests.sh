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

# Miscellaneous tests
tests="
    test_basic_cmd.expect
    test_env.expect
    test_ps1.expect
    test_export_ps1.expect
    test_export.expect
    test_unset.expect
    test_unset_function.expect
    test_local.expect
    test_func_scope.expect
    test_local_shadow.expect
    test_script.expect
    test_script_args.expect
    test_comments.expect
    test_source.expect
    test_sequence.expect
    test_andor.expect
    test_status.expect
    test_badcmd.expect
    test_badcmd_noninteractive.expect
    test_completion.expect
    test_envfile.expect
    test_type.expect
    test_dash_c.expect
    test_dash_c_quotes.expect
    test_echo_options.expect
    test_set_options.expect
    test_nounset.expect
    test_line_cont.expect
    test_if.expect
    test_function.expect
    test_assign.expect
    test_case.expect
    test_exit_trap.expect
    test_eval.expect
    test_exec_builtin.expect
    test_getopts.expect
    test_test.expect
    test_cond.expect
    test_printf.expect
    test_printf_escapes.expect
    test_source_args.expect
    test_time.expect
    test_times.expect
    test_command.expect
    test_command_v.expect
    test_command_V.expect
    test_command_p.expect
    test_true_builtin.expect
    test_false_builtin.expect
    test_colon.expect
    test_hash.expect
    test_version.expect
    test_ulimit.expect
    test_fc.expect
    test_negate.expect
    test_command_pv.expect
    test_command_pV.expect
    test_export_p.expect
    test_export_n.expect
    test_export_p_listing.expect
    test_export_n_unexport.expect
    test_set_list.expect
    test_function_keyword.expect
    test_test_bool.expect
    test_time_p.expect
    test_calloc_fail.expect
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
