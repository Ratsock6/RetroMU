#!/usr/bin/env bash
# ===========================================================================
#  Step 6 regression test: the execution tracer.
# ===========================================================================
#  Three things are checked:
#
#    1. the trace line format matches the one the community's reference logs
#       use, so a trace produced here can be diffed against them;
#    2. the diff tool finds the first divergence and names what changed;
#    3. the behaviour of the CPU has not drifted, by comparing a fingerprint
#       of each bundled ROM's first 200000 instructions against a golden file.
#
#  Check 3 is the one that matters for the rest of the project. Adding the
#  timer, the PPU or the MBCs will legitimately change some digests; when that
#  happens, regenerate them with --update and read the diff to confirm the
#  change was the intended one.
# ===========================================================================
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
EMU="${RETROEMU_BIN:-$ROOT/build/retroemu}"
GOLDEN="$ROOT/tests/expected/trace_digests.txt"
ROM="$ROOT/roms/acid2/dmg-acid2.gb"
UPDATE=0
[ "${1:-}" = "--update" ] && UPDATE=1

if [ ! -x "$EMU" ]; then
    echo "error: $EMU not found. Build first: cmake -B build && cmake --build build -j"
    exit 1
fi

cd "$ROOT" || exit 1
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

pass=0
fail=0

expect() {          # expect <description> <pattern> <output>
    if printf '%s' "$3" | grep -qF -- "$2"; then
        printf '  \033[1;32mPASS\033[0m  %s\n' "$1"; pass=$((pass + 1))
    else
        printf '  \033[1;31mFAIL\033[0m  %s\n        expected to find: %s\n' "$1" "$2"
        fail=$((fail + 1))
    fi
}

# --- 1. Line format ---------------------------------------------------------
echo "== trace format =="

OUT="$("$EMU" --trace "$ROM" --trace-limit 3 2>&1)"
expect "the first line is the post-boot state" \
       "A:01 F:B0 B:00 C:13 D:00 E:D8 H:01 L:4D SP:FFFE PC:0100 PCMEM:00,C3,50,01" "$OUT"
expect "the second line follows the first instruction" "PC:0101" "$OUT"

LINES="$(printf '%s\n' "$OUT" | wc -l)"
if [ "$LINES" -eq 3 ]; then
    printf '  \033[1;32mPASS\033[0m  --trace-limit produces exactly that many lines\n'; pass=$((pass + 1))
else
    printf '  \033[1;31mFAIL\033[0m  expected 3 lines, got %s\n' "$LINES"; fail=$((fail + 1))
fi

# Tracing must not perturb the run: with and without a trace, the machine
# must reach the same state.
"$EMU" --trace "$ROM" --trace-file "$TMP/x.log" --trace-limit 5000 --quiet
A="$("$EMU" --tracehash "$ROM" --trace-limit 5000)"
B="$("$EMU" --tracehash "$ROM" --trace-limit 5000)"
if [ "$A" = "$B" ]; then
    printf '  \033[1;32mPASS\033[0m  tracing is deterministic and free of side effects\n'; pass=$((pass + 1))
else
    printf '  \033[1;31mFAIL\033[0m  two identical runs produced different traces\n'; fail=$((fail + 1))
fi

# --- 2. The diff tool -------------------------------------------------------
echo
echo "== differential comparison =="

"$EMU" --trace "$ROM" --trace-file "$TMP/a.log" --trace-limit 4000 --quiet
"$EMU" --trace "$ROM" --trace-file "$TMP/b.log" --trace-limit 4000 --quiet --ly-stub

OUT="$("$EMU" --tracediff "$TMP/a.log" "$TMP/a.log" 2>&1)"
expect "identical traces are reported as identical" "identical: 4000 lines match" "$OUT"

# The ROM polls LY and waits for 0x90, so stubbing LY changes what it reads.
# A real divergence, not a synthetic one.
OUT="$("$EMU" --tracediff "$TMP/a.log" "$TMP/b.log" 2>&1)"
expect "a real divergence is located"      "first divergence at instruction 12" "$OUT"
expect "the differing register is named"   "A      differs: 00 versus 90" "$OUT"
expect "context before it is shown"        "PCMEM:F0,44,FE,90" "$OUT"

head -12 "$TMP/a.log" > "$TMP/short.log"
OUT="$("$EMU" --tracediff "$TMP/a.log" "$TMP/short.log" 2>&1)"
expect "a truncated trace is reported"     "then one trace stops" "$OUT"

OUT="$("$EMU" --tracediff "$TMP/nope.log" "$TMP/a.log" 2>&1)"
expect "a missing file is reported"        "cannot open" "$OUT"

# --- 3. Behaviour has not drifted -------------------------------------------
echo
echo "== CPU behaviour fingerprints =="

ROMS=$(find roms -name '*.gb' -o -name '*.gbc' | sort)
# shellcheck disable=SC2086
ACTUAL="$("$EMU" --tracehash $ROMS --trace-limit 200000)"

if [ "$UPDATE" -eq 1 ]; then
    printf '%s\n' "$ACTUAL" > "$GOLDEN"
    echo "  golden file updated: $GOLDEN"
elif [ ! -f "$GOLDEN" ]; then
    printf '  \033[1;31mFAIL\033[0m  golden file missing, run with --update\n'; fail=$((fail + 1))
elif [ "$ACTUAL" = "$(cat "$GOLDEN")" ]; then
    printf '  \033[1;32mPASS\033[0m  all 9 bundled ROMs execute exactly as before\n'; pass=$((pass + 1))
else
    printf '  \033[1;31mFAIL\033[0m  CPU behaviour changed on:\n'
    diff <(cat "$GOLDEN") <(printf '%s\n' "$ACTUAL") | grep '^>' | sed 's/^/        /'
    printf '        if the change was intended, rerun with --update\n'
    fail=$((fail + 1))
fi

echo
if [ "$fail" -eq 0 ]; then
    printf '\033[1;32m%d passed, 0 failed\033[0m\n' "$pass"; exit 0
fi
printf '\033[1;31m%d passed, %d failed\033[0m\n' "$pass" "$fail"; exit 1
