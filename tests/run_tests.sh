#!/bin/sh
set -e

if ! command -v expect >/dev/null; then
    echo "Expect is not installed. Please install Expect to run tests." >&2
    exit 1
fi

TMP_HOME=$(mktemp -d)
export HOME="$TMP_HOME"
export VUSH_FUNCFILE=/dev/null
trap 'rm -rf "$TMP_HOME"' EXIT

failed=0

tests="
    test_basic_cmd.expect
    test_env.expect
    test_ps1.expect
    test_export_ps1.expect
    test_ps1_cmdsub.expect
    test_pwd.expect
    test_cd_dash.expect
    test_cdpath.expect
    test_pushd.expect
    test_dirs.expect
    test_tilde_user.expect
    test_export.expect
    test_unset.expect
    test_unset_function.expect
    test_local.expect
    test_func_scope.expect
    test_local_shadow.expect
    test_script.expect
    test_script_args.expect
    test_comments.expect
    test_history.expect
    test_history_clear.expect
    test_history_limit.expect
    test_history_delete.expect
    test_pipe.expect
    test_redir.expect
    test_assign_redir.expect
    test_source.expect
    test_fg.expect
    test_bg.expect
    test_kill.expect
    test_sequence.expect
    test_andor.expect
    test_glob.expect
    test_alias.expect
    test_alias_flags.expect
    test_alias_update.expect
    test_alias_persist.expect
    test_unalias_a.expect
    test_status.expect
    test_badcmd.expect
    test_badcmd_noninteractive.expect
    test_cmdsub.expect
    test_cmdsub_regress.expect
    test_lineedit.expect
    test_completion.expect
    test_completion_path.expect
    test_reverse_search.expect
    test_forward_search.expect
    test_err_redir.expect
    test_fd_dup.expect
    test_vushrc.expect
    test_envfile.expect
    test_var_brace.expect
    test_param_expand.expect
    test_param_inline.expect
    test_unmatched.expect
    test_jobs.expect
    test_type.expect
    test_custom_histfile.expect
    test_custom_aliasfile.expect
    test_dash_c.expect
    test_dash_c_quotes.expect
    test_echo_options.expect
    test_set_options.expect
    test_nounset.expect
    test_heredoc.expect
    test_herestring.expect
    test_heredoc_unterminated.expect
    test_line_cont.expect
    test_if.expect
    test_for.expect
    test_for_shellvar.expect
    test_for_env.expect
    test_while.expect
    test_until.expect
    test_function.expect
    test_assign.expect
    test_arith.expect
    test_arith_expr.expect
    test_arith_complex.expect
    test_read.expect
    test_read_eof.expect
    test_read_signal.expect
    test_case.expect
    test_trap.expect
    test_exit_trap.expect
    test_eval.expect
    test_exec_builtin.expect
    test_getopts.expect
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
    test_for_arith.expect
    test_printf.expect
    test_printf_escapes.expect
    test_select.expect
    test_pipefail.expect
    test_noclobber.expect
    test_param_replace.expect
    test_source_args.expect
    test_bang_numeric.expect
    test_bang_words.expect
    test_time.expect
    test_times.expect
    test_command.expect
    test_command_v.expect
    test_command_V.expect
    test_command_p.expect
    test_param_substring.expect
    test_param_error.expect
    test_wait.expect
    test_umask.expect
    test_true_builtin.expect
    test_false_builtin.expect
    test_colon.expect
    test_readonly.expect
    test_hash.expect
    test_heredoc_dash.expect
    test_heredoc_tabs.expect
    test_version.expect
    test_ulimit.expect
    test_cd_P.expect
    test_pwd_options.expect
    test_fc.expect
    test_trap_p.expect
    test_pid_params.expect
    test_negate.expect
    test_jobs_l.expect
    test_jobs_p.expect
    test_kill_s.expect
    test_command_pv.expect
    test_command_pV.expect
    test_kill_l.expect
    test_kill_l_num.expect
    test_export_p.expect
    test_export_n.expect
    test_export_p_listing.expect
    test_export_n_unexport.expect
    test_readonly_p.expect
    test_trap_no_args.expect
    test_umask_symbolic.expect
    test_set_list.expect
    test_function_keyword.expect
    test_trap_l.expect
    test_fg_default.expect
    test_bg_default.expect
    test_test_bool.expect
    test_time_p.expect
    test_base_arith.expect
    test_path_blank.expect
    test_path_long.expect
    test_command_v_path_long.expect
    test_dquote_escape.expect
    test_calloc_fail.expect
"
for test in $tests; do
    echo "Running $test"
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
