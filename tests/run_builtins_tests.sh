#!/bin/sh
set -e

if ! command -v expect >/dev/null; then
    echo "Expect is not installed; skipping built-in command tests." >&2
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
test_basic_cmd.expect
test_basic_cmd_regress.expect
test_env.expect
test_ps1.expect
test_ps1_cmdsub.expect
test_pwd.expect
test_cd_dash.expect
test_cdpath.expect
test_pushd.expect
test_dirs.expect
test_tilde_user.expect
test_script.expect
test_script_args.expect
test_comments.expect
test_pipe.expect
test_redir.expect
test_source.expect
test_fg.expect
test_fg_percent.expect
test_bg.expect
test_bg_percent.expect
test_kill.expect
test_sequence.expect
test_andor.expect
test_glob.expect
test_status.expect
test_badcmd.expect
test_badcmd_noninteractive.expect
test_empty_cmd.expect
test_cmdsub.expect
test_cmdsub_regress.expect
test_completion.expect
test_completion_path.expect
test_err_redir.expect
test_fd_dup.expect
test_vushrc.expect
test_envfile.expect
test_unmatched.expect
test_jobs.expect
test_type.expect
test_type_t.expect
test_dash_c.expect
test_dash_c_quotes.expect
test_dash_c_echo.expect
test_echo_options.expect
test_heredoc.expect
test_herestring.expect
test_heredoc_unterminated.expect
test_line_cont.expect
test_if.expect
test_for.expect
test_for_env.expect
test_while.expect
test_until.expect
test_function.expect
test_read.expect
test_read_eof.expect
test_read_signal.expect
test_read_p.expect
test_read_n.expect
test_read_s.expect
test_read_t.expect
test_read_u.expect
test_case.expect
test_case_posix.expect
test_trap.expect
test_exit_trap.expect
test_eval.expect
test_exec_builtin.expect
test_getopts.expect
test_getopts_opterr.expect
test_subshell.expect
test_brace_group.expect
test_break.expect
test_continue.expect
test_continue_n.expect
test_test.expect
test_cond.expect
test_ls_l.expect
test_process_sub.expect
test_array.expect
test_brace_expand.expect
test_printf.expect
test_printf_escapes.expect
test_printf_v.expect
test_printf_long.expect
test_select.expect
test_pipefail.expect
test_noclobber.expect
test_source_args.expect
test_time.expect
test_times.expect
test_command.expect
test_command_v.expect
test_command_V.expect
test_command_p.expect
test_wait.expect
test_umask.expect
test_true_builtin.expect
test_false_builtin.expect
test_colon.expect
test_hash.expect
test_hash_p.expect
test_hash_d.expect
test_heredoc_dash.expect
test_heredoc_tabs.expect
test_heredoc_expand.expect
test_heredoc_noexpand.expect
test_heredoc_dquote.expect
test_version.expect
test_ulimit.expect
test_cd_P.expect
test_cd_P_dangle.expect
test_pwd_options.expect
test_fc.expect
test_trap_p.expect
test_trap_p_signal.expect
test_negate.expect
test_negate_multi.expect
test_jobs_l.expect
test_jobs_p.expect
test_jobs_r.expect
test_jobs_s.expect
test_jobs_n.expect
test_kill_s.expect
test_command_pv.expect
test_command_pV.expect
test_kill_l.expect
test_kill_l_num.expect
test_trap_no_args.expect
test_umask_symbolic.expect
test_function_keyword.expect
test_trap_l.expect
test_fg_default.expect
test_bg_default.expect
test_test_bool.expect
test_time_p.expect
test_path_blank.expect
test_path_long.expect
test_command_v_path_long.expect
test_dquote_escape.expect
test_ifs_split.expect
test_ifs_empty.expect
test_calloc_fail.expect
test_fc_fork_fail.expect
arithmetic_basic.expect
arithmetic_cmd.expect
arithmetic_errors.expect
arithmetic_forloop.expect
arithmetic_overflow.expect
arithmetic_compound.expect
test_pipe_cr.expect
test_set_o.expect
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
        test_lineedit.expect|\
        test_fc.expect)
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
