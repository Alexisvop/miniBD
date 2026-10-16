#!/bin/sh
# Run a repeatable MiniDB demo against a fresh temporary database.
set -eu

MINIDB=${1:-./build/minidb}
if [ ! -x "$MINIDB" ]; then
  printf 'MiniDB executable not found: %s\nBuild it first with cmake.\n' "$MINIDB" >&2
  exit 1
fi

TEMP_DIR=$(mktemp -d)
trap 'rm -rf "$TEMP_DIR"' EXIT HUP INT TERM
DB="$TEMP_DIR/demo.db"

printf '%s\n' '=== Session 1: insert, update, delete and inspect ==='
"$MINIDB" "$DB" < examples/demo.txt

printf '\n%s\n' '=== Session 2: reopened database ==='
printf '%s\n' 'SELECT 1' 'SELECT 2' 'SELECT_ALL' '.stats' '.exit' | "$MINIDB" "$DB"

printf '\n%s\n' 'Demo complete. Temporary database removed on exit.'
