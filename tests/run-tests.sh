#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT_DIR/build/rcc"
TMP_DIR="$ROOT_DIR/build/tests"

mkdir -p "$TMP_DIR"

run_ok() {
  local input="$1"
  local expected_status="$2"
  local output="$TMP_DIR/$(basename "$input" .rs).ll"

  rm -f "$output"
  "$BIN" "$input" "$output"
  test -s "$output"

  if command -v lli >/dev/null 2>&1; then
    set +e
    lli "$output" >/dev/null
    local actual_status=$?
    set -e
    local expected_exit=$((expected_status & 255))
    if [[ "$actual_status" -ne "$expected_exit" ]]; then
      echo "expected $input to exit $expected_exit, got $actual_status" >&2
      exit 1
    fi
  fi
}

run_fail() {
  local input="$1"
  local expected_message="$2"
  local output="$TMP_DIR/$(basename "$input" .rs).ll"
  local error_output="$TMP_DIR/$(basename "$input" .rs).err"

  rm -f "$output" "$error_output"
  if "$BIN" "$input" "$output" 2>"$error_output"; then
    echo "expected failure for $input" >&2
    exit 1
  fi
  if ! grep -Fq "$expected_message" "$error_output"; then
    echo "expected diagnostic '$expected_message' for $input" >&2
    echo "actual diagnostics:" >&2
    cat "$error_output" >&2
    exit 1
  fi
}

run_ok "$ROOT_DIR/tests/basic_main.rs" 42
run_ok "$ROOT_DIR/tests/function_call.rs" 42
run_ok "$ROOT_DIR/tests/trailing_commas.rs" 42
run_ok "$ROOT_DIR/tests/implicit_return.rs" 15
run_ok "$ROOT_DIR/tests/let_inference.rs" 42
run_ok "$ROOT_DIR/tests/if_without_else.rs" 2
run_ok "$ROOT_DIR/tests/if_else.rs" 10
run_ok "$ROOT_DIR/tests/else_if.rs" 2
run_ok "$ROOT_DIR/tests/break_continue.rs" 18
run_ok "$ROOT_DIR/tests/block_scope.rs" 42
run_ok "$ROOT_DIR/tests/comments.rs" 42
run_ok "$ROOT_DIR/tests/i32_min.rs" -2147483648
run_ok "$ROOT_DIR/tests/empty_stmt.rs" 3
run_ok "$ROOT_DIR/tests/unary_plus.rs" 42
run_ok "$ROOT_DIR/tests/loop_stmt.rs" 9
run_ok "$ROOT_DIR/tests/loop_return.rs" 7
run_ok "$ROOT_DIR/tests/unary_and_mod.rs" 9
run_ok "$ROOT_DIR/tests/while_loop.rs" 10
run_ok "$ROOT_DIR/tests/precedence.rs" 7
run_ok "$ROOT_DIR/tests/expression_stmt.rs" 0
run_ok "$ROOT_DIR/tests/too_many_functions.rs" 0
run_fail "$ROOT_DIR/tests/out_of_scope.rs" "use of undefined variable 'hidden'"
run_fail "$ROOT_DIR/tests/duplicate_same_scope.rs" "duplicate variable 'value'"
run_fail "$ROOT_DIR/tests/main_with_args.rs" "expected fn main() -> i32"
run_fail "$ROOT_DIR/tests/integer_overflow.rs" "integer literal overflow"
run_fail "$ROOT_DIR/tests/i32_literal_overflow.rs" "integer literal overflow"
run_fail "$ROOT_DIR/tests/i32_min_positive_overflow.rs" "integer literal overflow"
run_fail "$ROOT_DIR/tests/unterminated_comment.rs" "unterminated block comment"
run_fail "$ROOT_DIR/tests/malformed_block_comment.rs" "unterminated block comment"
run_fail "$ROOT_DIR/tests/nested_implicit_return.rs" "expected ';' after expression"
run_fail "$ROOT_DIR/tests/double_comma.rs" "expected expression, found ,"
run_fail "$ROOT_DIR/tests/divide_by_zero.rs" "division by zero"
run_fail "$ROOT_DIR/tests/missing_rhs.rs" "expected expression, found ;"
run_fail "$ROOT_DIR/tests/break_outside_loop.rs" "'break' used outside of loop"
run_fail "$ROOT_DIR/tests/continue_outside_loop.rs" "'continue' used outside of loop"
run_fail "$ROOT_DIR/tests/undefined_name.rs" "use of undefined variable 'missing'"
run_fail "$ROOT_DIR/tests/unreachable_undefined.rs" "use of undefined variable 'missing'"
run_fail "$ROOT_DIR/tests/assign_immutable.rs" "variable 'value' is not mutable"
run_fail "$ROOT_DIR/tests/missing_else_return.rs" "must not fall through without returning"
run_fail "$ROOT_DIR/tests/wrong_arity.rs" "expects 2 args, got 1"
run_fail "$ROOT_DIR/tests/missing_return.rs" "must not fall through without returning"

echo "tests passed"
