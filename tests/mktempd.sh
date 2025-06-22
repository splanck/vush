#!/bin/sh
# POSIX-compatible mktemp -d replacement
set -e
tmp=$(mktemp)
rm -f "$tmp"
mkdir "$tmp"
printf '%s\n' "$tmp"
