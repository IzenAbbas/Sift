# Enforce run and compile parity
#!/bin/bash
# run_tests.sh -- runs every test in tests/ and prints PASS/FAIL for each.
#
# Kinds of test (by file name):
#   tests/tok_NAME.sift   ./sift tokens output must equal tok_NAME.expected
#   tests/ast_NAME.sift   ./sift ast output must equal ast_NAME.expected
#   tests/NN_NAME.sift    program output (stdout) must equal NN_NAME.expected,
#                         and the exit code must equal NN_NAME.exitcode
#                         (if that file exists; otherwise 0)
#   tests/err_NAME.sift   the program must fail (non-zero exit) and stderr
#                         must contain the text in err_NAME.expected
#
# NN_ and err_ tests are run in every mode listed in MODES:
#   run        ./sift run file.sift
#   compile    ./sift compile file.sift -o prog, then ./prog
#   optimized  ./sift compile file.sift -O -o prog (opt -O2 first), then ./prog
# so the interpreter and the compiled program must give the same output
# and the same exit code.
# Usage:  ./run_tests.sh                       (run and compile)
#         ./run_tests.sh run                   (interpreter only)
#         ./run_tests.sh run compile optimized (also with opt -O2)

cd "$(dirname "$0")" || exit 1

MODES="run compile"
if [ $# -gt 0 ]; then
    MODES="$*"
fi

SIFT=./sift
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

pass=0
fail=0

report() {   # report RESULT NAME [DETAIL]
    if [ "$1" = PASS ]; then
        pass=$((pass + 1))
        echo "PASS  $2"
    else
        fail=$((fail + 1))
        echo "FAIL  $2"
        [ -n "$3" ] && echo "$3" | sed 's/^/      /'
    fi
}

# Run a .sift program in the given mode. Writes $TMP/out, $TMP/err and
# sets $status to the exit code of the program (or of the compiler, if
# compiling failed).
run_program() {   # run_program MODE FILE
    local mode=$1 file=$2
    if [ "$mode" = run ]; then
        $SIFT run "$file" > "$TMP/out" 2> "$TMP/err"
        status=$?
    else
        local flags=""
        [ "$mode" = optimized ] && flags="-O"
        rm -f "$TMP/prog"
        if $SIFT compile "$file" $flags -o "$TMP/prog" > "$TMP/out" 2> "$TMP/err"; then
            "$TMP/prog" > "$TMP/out" 2> "$TMP/err"
            status=$?
        else
            status=$?
        fi
    fi
}

# --- debug-output tests (tokens / ast) ---
for kind in tok ast; do
    cmd=tokens
    [ $kind = ast ] && cmd=ast
    for src in tests/${kind}_*.sift; do
        [ -e "$src" ] || continue
        name=$(basename "$src" .sift)
        $SIFT $cmd "$src" > "$TMP/out" 2> "$TMP/err"
        if diff -u "tests/$name.expected" "$TMP/out" > "$TMP/diff"; then
            report PASS "$name"
        else
            report FAIL "$name" "$(head -20 "$TMP/diff"; cat "$TMP/err")"
        fi
    done
done

# --- program tests, in every mode ---
for mode in $MODES; do
    for src in tests/[0-9]*.sift; do
        [ -e "$src" ] || continue
        name=$(basename "$src" .sift)
        want_status=0
        [ -f "tests/$name.exitcode" ] && want_status=$(cat "tests/$name.exitcode")
        run_program "$mode" "$src"
        if ! diff -u "tests/$name.expected" "$TMP/out" > "$TMP/diff"; then
            report FAIL "$name [$mode]" "$(head -20 "$TMP/diff"; head -5 "$TMP/err")"
        elif [ "$status" != "$want_status" ]; then
            report FAIL "$name [$mode]" "exit code $status, expected $want_status
$(head -5 "$TMP/err")"
        else
            report PASS "$name [$mode]"
        fi
    done

    for src in tests/err_*.sift; do
        [ -e "$src" ] || continue
        name=$(basename "$src" .sift)
        want=$(cat "tests/$name.expected")
        run_program "$mode" "$src"
        if [ "$status" = 0 ]; then
            report FAIL "$name [$mode]" "expected failure, but exit code was 0"
        elif ! grep -qF -- "$want" "$TMP/err"; then
            report FAIL "$name [$mode]" "stderr does not contain: $want
got: $(head -5 "$TMP/err")"
        else
            report PASS "$name [$mode]"
        fi
    done
done

echo
echo "$pass passed, $fail failed, $((pass + fail)) total"
[ $fail -eq 0 ]
