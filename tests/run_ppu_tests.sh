#!/usr/bin/env bash
# ===========================================================================
#  Step 8 regression test: the PPU state machine.
# ===========================================================================
#  No pixels are drawn yet; step 9 does that. What is checked here is that the
#  screen is swept the way the hardware sweeps it, because that is what games
#  synchronise on:
#
#    - LY (0xFF44) tells a game which line is being swept. Games poll it.
#    - VBlank is the only safe window to write video memory, so games do all
#      their drawing inside the interrupt it raises.
#
#  The proof that it works is dmg-acid2: before this step it spun forever
#  waiting for LY to reach 144, and the step 6 tracer showed exactly that at
#  instruction 12. It now loads its graphics and stops.
# ===========================================================================
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
EMU="${RETROEMU_BIN:-$ROOT/build/retroemu}"
ROM="$ROOT/roms/acid2/dmg-acid2.gb"

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

# --- The sweep is visible from the debugger --------------------------------
echo "== the screen is being swept =="

OUT="$(printf 'i\nq\n' | "$EMU" --debug "$ROM" 2>&1)"
expect "the screen reports as on"       "screen       on" "$OUT"

OUT="$(printf 'f\ni\nq\n' | "$EMU" --debug "$ROM" 2>&1)"
expect "one image is drawn per frame"   "frames drawn 1" "$OUT"
expect "a frame ends in VBlank"         "mode 1 VBlank" "$OUT"
expect "and it ends on line 144"        "LY 144" "$OUT"

# A frame now ends when the PPU says so, at line 144, rather than after a
# fixed cycle count. 144 x 456 = 65664.
expect "a frame ends at 65664 system cycles" "ran 1 frame(s), 65664 system cycles" "$OUT"

# --- dmg-acid2 no longer hangs ----------------------------------------------
echo
echo "== dmg-acid2 gets past its wait loop =="

# Before this step the ROM sat forever on LDH A,($44) / CP $90 / JR NZ,-6.
# It now runs on and fills video memory.
OUT="$(printf 'f 10\nm $8030 16\nm $FE00 16\nq\n' | "$EMU" --debug "$ROM" 2>&1)"

VRAM_LINE="$(printf '%s' "$OUT" | grep -oE '\$8030  ([0-9A-F]{2} ){16}' | head -1)"
if [ -n "$VRAM_LINE" ] && printf '%s' "$VRAM_LINE" | grep -qvE '\$8030  (00 ){16}'; then
    printf '  \033[1;32mPASS\033[0m  tile data was written into VRAM\n'; pass=$((pass + 1))
else
    printf '  \033[1;31mFAIL\033[0m  VRAM is still empty at $8030\n'; fail=$((fail + 1))
fi

OAM_LINE="$(printf '%s' "$OUT" | grep -oE '\$FE00  ([0-9A-F]{2} ){16}' | head -1)"
if [ -n "$OAM_LINE" ] && printf '%s' "$OAM_LINE" | grep -qvE '\$FE00  (00 ){16}'; then
    printf '  \033[1;32mPASS\033[0m  sprite attributes were written into OAM\n'; pass=$((pass + 1))
else
    printf '  \033[1;31mFAIL\033[0m  OAM is still empty\n'; fail=$((fail + 1))
fi

# --- Nothing that used to pass has broken -----------------------------------
echo
echo "== the earlier steps still hold =="

OUT="$("$EMU" --mooneye roms/mooneye/acceptance/div_timing.gb \
                        roms/mooneye/acceptance/intr_timing.gb --max-cycles 60000000 2>&1)"
expect "div_timing still passes"  "PASS  roms/mooneye/acceptance/div_timing.gb"  "$OUT"
expect "intr_timing still passes" "PASS  roms/mooneye/acceptance/intr_timing.gb" "$OUT"

# --- Built-in checks ---------------------------------------------------------
echo
echo "== built-in PPU checks =="
if "$EMU" --cpucheck --quiet >/dev/null 2>&1; then
    printf '  \033[1;32mPASS\033[0m  31 PPU checks inside --cpucheck\n'; pass=$((pass + 1))
else
    printf '  \033[1;31mFAIL\033[0m  --cpucheck\n'
    "$EMU" --cpucheck | grep -A2 FAIL
    fail=$((fail + 1))
fi

# --- Speed (subject V.3) -----------------------------------------------------
echo
echo "== emulation speed (subject V.3) =="
START=$(date +%s%N)
"$EMU" --run "$ROM" --quiet --max-cycles 251658240 >/dev/null 2>&1
END=$(date +%s%N)
ELAPSED_MS=$(( (END - START) / 1000000 ))
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
