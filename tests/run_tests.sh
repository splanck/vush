#!/bin/sh
set -e
failed=0

tests="test_basic_cmd.expect test_env.expect test_ps1.expect test_ps1_cmdsub.expect test_pwd.expect test_cd_dash.expect test_cdpath.expect test_pushd.expect test_dirs.expect test_tilde_user.expect test_export.expect test_unset.expect test_script.expect test_script_args.expect test_comments.expect test_history.expect test_history_clear.expect test_history_limit.expect test_history_delete.expect test_pipe.expect test_redir.expect test_source.expect test_fg.expect test_bg.expect test_kill.expect test_sequence.expect test_andor.expect test_glob.expect test_alias.expect test_alias_persist.expect test_status.expect test_badcmd.expect test_badcmd_noninteractive.expect test_cmdsub.expect test_lineedit.expect test_completion.expect test_reverse_search.expect test_forward_search.expect test_err_redir.expect test_fd_dup.expect test_vushrc.expect test_var_brace.expect test_param_expand.expect test_unmatched.expect test_jobs.expect test_type.expect test_custom_histfile.expect test_custom_aliasfile.expect test_dash_c.expect test_dash_c_quotes.expect test_set_options.expect test_heredoc.expect test_herestring.expect test_heredoc_unterminated.expect test_line_cont.expect test_if.expect test_for.expect test_while.expect test_until.expect test_function.expect test_assign.expect test_arith.expect test_read.expect test_read_eof.expect test_case.expect test_trap.expect test_eval.expect test_exec_builtin.expect test_getopts.expect test_subshell.expect test_brace_group.expect test_break.expect test_continue.expect test_test.expect test_cond.expect test_ls_l.expect test_process_sub.expect"

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
