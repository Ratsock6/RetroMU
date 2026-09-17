#!/usr/bin/env bash
# ===========================================================================
#  Step 7 regression test: interrupts and timer.
# ===========================================================================
#  This is the first step judged by ROMs from the subject's own bundle.
#  mooneye's div_timing and intr_timing check on which exact cycle, INSIDE an
#  instruction, DIV increments and the interrupt flag is sampled. They are the
#  examination that decision D8 (tick-on-access) was designed to pass.
#
#  Those ROMs report neither over the link port nor in a way we can read
#  without a screen, so --mooneye implements their convention instead: the
#  registers are loaded with the start of the Fibonacci sequence on success,
#  followed by LD B,B used as a software breakpoint.
# ===========================================================================
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
EMU="${RETROEMU_BIN:-$ROOT/build/retroemu}"

if [ ! -x "$EMU" ]; then
    echo "error: $EMU not found. Build first: cmake -B build && cmake --build build -j"
    exit 1
fi

cd "$ROOT" || exit 1
pass=0
fail=0

# Colour codes sit between the verdict and the path, so they are stripped
# before matching.
strip_ansi() { sed 's/\x1b\[[0-9;]*m//g'; }

expect() {          # expect <description> <pattern> <output>
    if printf '%s' "$3" | strip_ansi | grep -qF -- "$2"; then
        printf '  \033[1;32mPASS\033[0m  %s\n' "$1"; pass=$((pass + 1))
    else
        printf '  \033[1;31mFAIL\033[0m  %s\n        expected to find: %s\n' "$1" "$2"
        fail=$((fail + 1))
    fi
}

# --- The bundle's acceptance ROMs -------------------------------------------
echo "== mooneye acceptance ROMs (from the subject's bundle) =="

OUT="$("$EMU" --mooneye roms/mooneye/acceptance/div_timing.gb \
                        roms/mooneye/acceptance/intr_timing.gb \
                        --max-cycles 60000000 2>&1)"
expect "div_timing passes"  "PASS  roms/mooneye/acceptance/div_timing.gb"  "$OUT"
expect "intr_timing passes" "PASS  roms/mooneye/acceptance/intr_timing.gb" "$OUT"

# --- The mooneye protocol itself --------------------------------------------
echo
echo "== the verdict protocol =="
expect "a passing ROM is recognised"  "PASS" \
       "$("$EMU" --mooneye roms/mooneye/acceptance/div_timing.gb --max-cycles 60000000 2>&1)"
# Since step 13 no ROM of the bundle reports an explicit failure any more, so
# the branch checked here is the other one: a ROM that reaches the marker with
# registers that are not the expected sequence. The acid2 ROMs do exactly
# that, because they use LD B,B for their own purposes.
expect "unexpected registers are reported"  "expected 3 5 8 13 21 34" \
       "$("$EMU" --mooneye roms/acid2/dmg-acid2.gb --max-cycles 30000000 2>&1)"
# A tiny cycle budget guarantees the marker is never reached. dmg-acid2 is not
# used here: it contains an LD B,B of its own, which it uses to tell a debugger
# the screen is ready to be compared.
expect "a ROM that never finishes is reported" "never reached the end marker" \
       "$("$EMU" --mooneye roms/mooneye/acceptance/div_timing.gb --max-cycles 1000 2>&1)"

# --- Built-in timer checks ---------------------------------------------------
echo
echo "== built-in timer checks =="
if "$EMU" --cpucheck --quiet >/dev/null 2>&1; then
    printf '  \033[1;32mPASS\033[0m  17 timer checks inside --cpucheck\n'; pass=$((pass + 1))
else
    printf '  \033[1;31mFAIL\033[0m  --cpucheck\n'
    "$EMU" --cpucheck | grep -A2 FAIL
    fail=$((fail + 1))
fi

# --- Speed (subject V.3) -----------------------------------------------------
echo
echo "== emulation speed (subject V.3) =="
START=$(date +%s%N)
"$EMU" --run roms/acid2/dmg-acid2.gb --quiet --max-cycles 251658240 >/dev/null 2>&1
END=$(date +%s%N)
ELAPSED_MS=$(( (END - START) / 1000000 ))
# 251658240 cycles is 60 emulated seconds. Real time must stay well under that.
if [ "$ELAPSED_MS" -lt 60000 ]; then
    printf '  \033[1;32mPASS\033[0m  60 emulated seconds took %d ms (%dx real time)\n' \
           "$ELAPSED_MS" $(( 60000 / (ELAPSED_MS > 0 ? ELAPSED_MS : 1) ))
    pass=$((pass + 1))
else
    printf '  \033[1;31mFAIL\033[0m  60 emulated seconds took %d ms: slower than real time\n' "$ELAPSED_MS"
    fail=$((fail + 1))
fi

echo
if [ "$fail" -eq 0 ]; then
    printf '\033[1;32m%d passed, 0 failed\033[0m\n' "$pass"; exit 0
fi
printf '\033[1;31m%d passed, %d failed\033[0m\n' "$pass" "$fail"; exit 1
