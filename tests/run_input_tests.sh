#!/usr/bin/env bash
# ===========================================================================
#  Step 11 regression test: real-time loop and inputs.
# ===========================================================================
#  Two explicit requirements of the subject:
#
#    V.3  "Your emulator will run at normal speed without adversely affect the
#          operations of the GUI."
#    V.4  the control pad and the four action buttons.
#
#  Both are measured rather than asserted. The window runs under SDL's dummy
#  video driver so the loop, its pacing and its event handling are the real
#  ones, with no display attached.
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

# --- Subject V.4: the eight buttons -----------------------------------------
echo "== subject V.4: the control pad and the four action buttons =="

OUT="$(printf 'k\nq\n' | "$EMU" --debug "$ROM" 2>&1)"
expect "JOYP reads 0xCF on a fresh machine" "JOYP reads \$CF" "$OUT"

# Select the action half, then press A.
OUT="$(printf 'w $FF00 $10\nk a\nq\n' | "$EMU" --debug "$ROM" 2>&1)"
expect "pressing A clears its wire"        "a pressed, JOYP reads \$DE" "$OUT"

# Select the pad half, then press Right.
OUT="$(printf 'w $FF00 $20\nk right\nq\n' | "$EMU" --debug "$ROM" 2>&1)"
expect "pressing Right clears its wire"    "right pressed, JOYP reads \$EE" "$OUT"

# A button of the half that is not selected must stay invisible.
OUT="$(printf 'w $FF00 $10\nk right\nq\n' | "$EMU" --debug "$ROM" 2>&1)"
expect "the other half stays invisible"    "right pressed, JOYP reads \$DF" "$OUT"

OUT="$(printf 'w $FF00 $10\nk a\nk a 0\nq\n' | "$EMU" --debug "$ROM" 2>&1)"
expect "releasing restores the wire"       "a released, JOYP reads \$DF" "$OUT"

OUT="$(printf 'k nonsense\nq\n' | "$EMU" --debug "$ROM" 2>&1)"
expect "an unknown button is reported"     "unknown button" "$OUT"

# All eight names must be accepted.
ALL_OK=1
for name in up down left right a b start select; do
    OUT="$(printf 'k %s\nq\n' "$name" | "$EMU" --debug "$ROM" 2>&1)"
    printf '%s' "$OUT" | grep -qF "$name pressed" || ALL_OK=0
done
if [ "$ALL_OK" -eq 1 ]; then
    printf '  \033[1;32mPASS\033[0m  all eight buttons are addressable\n'; pass=$((pass + 1))
else
    printf '  \033[1;31mFAIL\033[0m  at least one button name was rejected\n'; fail=$((fail + 1))
fi

# --- Subject V.3: normal speed ----------------------------------------------
echo
echo "== subject V.3: normal speed, and a responsive interface =="

# 180 frames must take 180 / 59.727 = 3.014 seconds of real time. Faster means
# the pacing is not working; slower means the emulation cannot keep up.
START=$(date +%s%N)
OUT="$(SDL_VIDEODRIVER=dummy "$EMU" "$ROM" --frames 180 2>&1)"
END=$(date +%s%N)
ELAPSED_MS=$(( (END - START) / 1000000 ))

expect "the window presented every frame"  "frames presented : 180" "$OUT"
expect "no frame arrived late"             "frames late      : 0" "$OUT"

# Allow 2.7 to 3.4 seconds: tight enough to catch a broken pacer, loose enough
# to survive a loaded machine.
if [ "$ELAPSED_MS" -ge 2700 ] && [ "$ELAPSED_MS" -le 3400 ]; then
    printf '  \033[1;32mPASS\033[0m  180 frames took %d ms (59.727 fps means 3014 ms)\n' "$ELAPSED_MS"
    pass=$((pass + 1))
else
    printf '  \033[1;31mFAIL\033[0m  180 frames took %d ms, expected about 3014\n' "$ELAPSED_MS"
    fail=$((fail + 1))
fi

# Paused, the loop must still run and present, just not emulate.
OUT="$(SDL_VIDEODRIVER=dummy "$EMU" "$ROM" --frames 30 --paused 2>&1)"
expect "a paused emulator still presents"  "frames presented : 30" "$OUT"
expect "and emulates nothing"              "emulated time    : 0.000 s" "$OUT"

# With no cartridge the window still opens rather than failing.
OUT="$(SDL_VIDEODRIVER=dummy "$EMU" --frames 10 2>&1)"
expect "the window opens with no cartridge" "frames presented : 10" "$OUT"

# --- Built-in checks ---------------------------------------------------------
echo
echo "== built-in joypad checks =="
if "$EMU" --cpucheck --quiet >/dev/null 2>&1; then
    printf '  \033[1;32mPASS\033[0m  16 joypad checks inside --cpucheck\n'; pass=$((pass + 1))
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
