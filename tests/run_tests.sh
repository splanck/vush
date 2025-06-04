#!/bin/sh
set -e
failed=0
tests="test_env.expect test_ps1.expect test_pwd.expect test_cd_dash.expect test_tilde_user.expect test_export.expect test_unset.expect test_script.expect test_comments.expect test_history.expect test_pipe.expect test_redir.expect test_source.expect test_fg.expect test_kill.expect test_sequence.expect test_andor.expect test_glob.expect test_alias.expect test_status.expect test_cmdsub.expect test_lineedit.expect test_err_redir.expect test_vushrc.expect test_var_brace.expect"
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
