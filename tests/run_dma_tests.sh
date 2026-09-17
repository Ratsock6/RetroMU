#!/usr/bin/env bash
# ===========================================================================
#  Step 10 regression test: OAM DMA.
# ===========================================================================
#  Writing one byte to 0xFF46 starts a 160-byte copy into the sprite table
#  that proceeds on its own, one byte per machine cycle, while the CPU carries
#  on. It is the clearest example in the machine of an address that stores
#  nothing and starts something instead.
#
#  The judge is roms/mooneye/acceptance/oam_dma/basic.gb, the last ROM in the
#  subject's bundle that did not depend on the MBCs of step 13.
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

strip_ansi() { sed 's/\x1b\[[0-9;]*m//g'; }

expect() {          # expect <description> <pattern> <output>
    if printf '%s' "$3" | strip_ansi | grep -qF -- "$2"; then
        printf '  \033[1;32mPASS\033[0m  %s\n' "$1"; pass=$((pass + 1))
    else
        printf '  \033[1;31mFAIL\033[0m  %s\n        expected to find: %s\n' "$1" "$2"
        fail=$((fail + 1))
    fi
}

# --- The bundle's DMA ROM ---------------------------------------------------
echo "== the subject's bundle =="

OUT="$("$EMU" --mooneye roms/mooneye/acceptance/oam_dma/basic.gb --max-cycles 60000000 2>&1)"
expect "oam_dma/basic passes" "PASS  roms/mooneye/acceptance/oam_dma/basic.gb" "$OUT"

# Everything in the bundle that does not depend on the MBCs of step 13.
OUT="$("$EMU" --mooneye roms/mooneye/acceptance/div_timing.gb \
                        roms/mooneye/acceptance/intr_timing.gb \
                        roms/mooneye/acceptance/oam_dma/basic.gb \
                        --max-cycles 60000000 2>&1)"
expect "all three acceptance ROMs pass together" "3 passed, 0 failed" "$OUT"

# The four MBC ROMs still fail. Declared so the expectation stays explicit.
echo
echo "== still to come =="
OUT="$("$EMU" --mooneye roms/mooneye/mbc1/*.gb roms/mooneye/mbc2/*.gb roms/mooneye/mbc5/*.gb \
              --max-cycles 60000000 2>&1)"
expect "the four MBC ROMs still fail, as expected before step 13" "0 passed, 4 failed" "$OUT"

# --- Nothing regressed -------------------------------------------------------
echo
echo "== the sprite path still renders correctly =="

# Sprites now reach the table through the copier, so dmg-acid2 is the check
# that the copier did not break the picture.
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
"$EMU" --screenshot roms/acid2/dmg-acid2.gb --out "$TMP/acid2.ppm" --max-cycles 30000000 --quiet
if cmp -s "$TMP/acid2.ppm" "$ROOT/tests/expected/dmg-acid2-reference.ppm"; then
    printf '  \033[1;32mPASS\033[0m  dmg-acid2 still matches the reference pixel for pixel\n'
    pass=$((pass + 1))
else
    printf '  \033[1;31mFAIL\033[0m  dmg-acid2 no longer matches the reference\n'; fail=$((fail + 1))
fi

# --- Built-in checks ---------------------------------------------------------
echo
echo "== built-in DMA checks =="
if "$EMU" --cpucheck --quiet >/dev/null 2>&1; then
    printf '  \033[1;32mPASS\033[0m  10 DMA checks inside --cpucheck\n'; pass=$((pass + 1))
else
    printf '  \033[1;31mFAIL\033[0m  --cpucheck\n'
    "$EMU" --cpucheck | grep -A2 FAIL
    fail=$((fail + 1))
fi

echo
if [ "$fail" -eq 0 ]; then
    printf '\033[1;32m%d passed, 0 failed\033[0m\n' "$pass"; exit 0
fi
printf '\033[1;31m%d passed, %d failed\033[0m\n' "$pass" "$fail"; exit 1
