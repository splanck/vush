#!/bin/sh
set -e

if ! command -v expect >/dev/null; then
    echo "Error: the 'expect' command is not available." >&2
    echo "Install the expect package (e.g. 'sudo apt-get install expect') and try again." >&2
    exit 1
fi

failed=0

DIR="$(dirname "$0")"

"$DIR/run_alias_tests.sh" || failed=1
"$DIR/run_var_tests.sh" || failed=1
"$DIR/run_builtins_tests.sh" || failed=1
"$DIR/run_history_tests.sh" || failed=1

exit $failed

