#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT_DIR/build/rcc"
TMP_DIR="$ROOT_DIR/build/tests"

mkdir -p "$TMP_DIR"

run_ok() {
  local input="$1"
  local output="$TMP_DIR/$(basename "$input" .rs).ll"

  "$BIN" "$input" "$output"
  test -s "$output"
}

run_fail() {
  local input="$1"
  local output="$TMP_DIR/$(basename "$input" .rs).ll"

  if "$BIN" "$input" "$output"; then
    echo "expected failure for $input" >&2
    exit 1
  fi
}

run_ok "$ROOT_DIR/tests/basic_main.rs"
run_ok "$ROOT_DIR/tests/function_call.rs"
run_ok "$ROOT_DIR/tests/if_else.rs"
run_ok "$ROOT_DIR/tests/break_continue.rs"
run_ok "$ROOT_DIR/tests/block_scope.rs"
run_ok "$ROOT_DIR/tests/unary_and_mod.rs"
run_ok "$ROOT_DIR/tests/while_loop.rs"
run_ok "$ROOT_DIR/tests/precedence.rs"
run_fail "$ROOT_DIR/tests/out_of_scope.rs"
run_fail "$ROOT_DIR/tests/break_outside_loop.rs"
run_fail "$ROOT_DIR/tests/continue_outside_loop.rs"
run_fail "$ROOT_DIR/tests/undefined_name.rs"
run_fail "$ROOT_DIR/tests/assign_immutable.rs"
run_fail "$ROOT_DIR/tests/missing_else_return.rs"
run_fail "$ROOT_DIR/tests/wrong_arity.rs"
run_fail "$ROOT_DIR/tests/missing_return.rs"

echo "tests passed"
