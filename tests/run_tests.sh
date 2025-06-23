#!/bin/sh
set -e

if ! command -v expect >/dev/null; then
    echo "'expect' not found; skipping tests." >&2
    exit 0
fi

failed=0

DIR="$(dirname "$0")"

"$DIR/run_alias_tests.sh" || failed=1
"$DIR/run_var_tests.sh" || failed=1
"$DIR/run_builtins_tests.sh" || failed=1
"$DIR/run_history_tests.sh" || failed=1

exit $failed

