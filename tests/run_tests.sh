#!/bin/sh
set -e

USAGE="Usage: $0 [alias|fs|job|history|io|loop|expansion|misc]"

scripts="alias fs job history io loop expansion misc"

run_script() {
    sh "run_${1}_tests.sh" && return 0 || return 1
}

failed=0

if [ $# -gt 1 ]; then
    echo "$USAGE" >&2
    exit 1
fi

if [ $# -eq 1 ]; then
    cat_name="$1"
    case "$scripts" in
        *$cat_name*)
            run_script "$cat_name" || failed=1
            ;;
        *)
            echo "$USAGE" >&2
            exit 1
            ;;
    esac
else
    for cat in $scripts; do
        echo "=== Running $cat tests ==="
        if ! run_script "$cat"; then
            failed=1
        fi
    done
fi
exit $failed
