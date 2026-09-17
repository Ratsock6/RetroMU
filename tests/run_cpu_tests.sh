#!/usr/bin/env bash
# ===========================================================================
#  Step 4 regression test: the instruction set.
# ===========================================================================
#  Two layers:
#
#    1. The built-in self-test (--cpucheck). Needs no external file, so it
#       always runs. Covers the half-carry, the flags that must survive an
#       operation, instruction timing and interrupt dispatch.
#
#    2. blargg's cpu_instrs suite, if roms-dev/ has been populated by
#       ./tools/fetch_dev_roms.sh. Those ROMs are NOT committed: only the MIT
#       bundle belongs in this repository (subject Ch. VII, p.11). They report
#       through the link port, which the emulator captures, so they run with
#       no screen at all.
# ===========================================================================
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
EMU="${RETROEMU_BIN:-$ROOT/build/retroemu}"
BLARGG="$ROOT/roms-dev/cpu_instrs/individual"

if [ ! -x "$EMU" ]; then
    echo "error: $EMU not found. Build first: cmake -B build && cmake --build build -j"
    exit 1
fi

cd "$ROOT" || exit 1
fail=0

# --- 1. Built-in self-test -------------------------------------------------
echo "== built-in self-test =="
if "$EMU" --cpucheck --quiet; then
    :
else
    fail=$((fail + 1))
    "$EMU" --cpucheck | grep -A2 FAIL
fi

# --- 2. blargg's cpu_instrs ------------------------------------------------
echo
echo "== blargg cpu_instrs =="

if [ ! -d "$BLARGG" ]; then
    echo "  skipped: run ./tools/fetch_dev_roms.sh to download them"
    echo
    [ "$fail" -eq 0 ] && exit 0 || exit 1
fi

# Every test in the suite is expected to pass since step 7 added the timer.
# The list is kept so that a future known-incomplete case can be declared
# explicitly rather than silently skipped.
KNOWN_INCOMPLETE=""

passed=0
failed=0
expected=0

for rom in "$BLARGG"/*.gb; do
    name="$(basename "$rom" .gb)"
    out="$("$EMU" --run "$rom" --quiet --max-cycles 400000000 2>&1)"
    code=$?

    if [ "$code" -eq 0 ]; then
        printf '  \033[1;32mPASS\033[0m  %s\n' "$name"
        passed=$((passed + 1))
    elif [ -n "$KNOWN_INCOMPLETE" ] && printf '%s' "$KNOWN_INCOMPLETE" | grep -qF -- "$name"; then
        printf '  \033[1;33mKNOWN\033[0m %s (needs the timer, step 7)\n' "$name"
        expected=$((expected + 1))
    else
        printf '  \033[1;31mFAIL\033[0m  %s\n' "$name"
        printf '%s' "$out" | tr -d '\r' | tr '\n' ' ' | sed 's/^/        /;s/$/\n/'
        failed=$((failed + 1))
    fi
done

# instr_timing lives outside cpu_instrs but is the other blargg ROM that
# depends on the timer, so it is run here too.
TIMING="$ROOT/roms-dev/instr_timing/instr_timing.gb"
if [ -f "$TIMING" ]; then
    if "$EMU" --run "$TIMING" --quiet --max-cycles 400000000 >/dev/null 2>&1; then
        printf '  \033[1;32mPASS\033[0m  instr_timing\n'; passed=$((passed + 1))
    else
        printf '  \033[1;31mFAIL\033[0m  instr_timing\n'; failed=$((failed + 1))
    fi
fi

echo
printf '%d passed, %d failed, %d known-incomplete\n' "$passed" "$failed" "$expected"
[ "$failed" -gt 0 ] && fail=$((fail + 1))

echo
if [ "$fail" -eq 0 ]; then
    printf '\033[1;32mCPU tests OK\033[0m\n'; exit 0
fi
printf '\033[1;31mCPU tests FAILED\033[0m\n'; exit 1
