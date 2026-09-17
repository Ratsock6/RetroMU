#!/usr/bin/env bash
# ===========================================================================
#  Step 12 regression test: the graphical interface.
# ===========================================================================
#  Chapter IV of the subject (p.6): "You must have a GUI with at minimum:
#  load, play, pause." It is an explicit failure criterion, so each of the
#  three is exercised rather than merely present.
#
#  The window runs under SDL's dummy video driver and key presses are fed into
#  its event queue one per frame, so the interface is driven exactly as a
#  human would drive it, with no display and no human.
# ===========================================================================
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
EMU="${RETROEMU_BIN:-$ROOT/build/retroemu}"

if [ ! -x "$EMU" ]; then
    echo "error: $EMU not found. Build first: cmake -B build && cmake --build build -j"
    exit 1
fi

cd "$ROOT" || exit 1
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

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

reject() {          # reject <description> <pattern> <output>
    if printf '%s' "$3" | strip_ansi | grep -qF -- "$2"; then
        printf '  \033[1;31mFAIL\033[0m  %s (found "%s")\n' "$1" "$2"; fail=$((fail + 1))
    else
        printf '  \033[1;32mPASS\033[0m  %s\n' "$1"; pass=$((pass + 1))
    fi
}

# --- LOAD -------------------------------------------------------------------
echo "== load =="

# From the browser, with the keyboard: starting in roms/acid2, the listing is
# [..], cgb-acid2.gbc, dmg-acid2.gb. Two downs reach the third entry.
OUT="$(SDL_VIDEODRIVER=dummy "$EMU" roms/acid2/cgb-acid2.gbc --browser \
        --ui-keys "down,down,return" --frames 20 2>&1)"
expect "a cartridge loads from the browser" "loaded roms/acid2/dmg-acid2.gb" "$OUT"

# Walking into a directory, then out of it again.
OUT="$(SDL_VIDEODRIVER=dummy "$EMU" roms/acid2/cgb-acid2.gbc --browser \
        --ui-keys "return,down,return,down,down,return" --frames 30 2>&1)"
expect "directories can be walked into and out of" "loaded roms/acid2/" "$OUT"

# The browser opens on demand from a running emulator, with O or F1.
OUT="$(SDL_VIDEODRIVER=dummy "$EMU" roms/acid2/cgb-acid2.gbc \
        --ui-keys "o,down,down,return" --frames 25 2>&1)"
expect "the browser opens from the keyboard" "loaded roms/acid2/dmg-acid2.gb" "$OUT"

# Escape closes it without loading anything.
OUT="$(SDL_VIDEODRIVER=dummy "$EMU" roms/acid2/cgb-acid2.gbc --browser \
        --ui-keys "down,down,escape" --frames 20 2>&1)"
reject "escape cancels without loading" "loaded" "$OUT"

# Starting with no cartridge at all, which is how a corrector may well start.
OUT="$(SDL_VIDEODRIVER=dummy "$EMU" --browser --frames 10 2>&1)"
expect "the emulator starts with no cartridge" "frames presented : 10" "$OUT"

# --- PLAY and PAUSE ----------------------------------------------------------
echo
echo "== play and pause =="

OUT="$(SDL_VIDEODRIVER=dummy "$EMU" roms/acid2/dmg-acid2.gb --ui-keys "space" --frames 20 2>&1)"
expect "space pauses"  "paused" "$OUT"

OUT="$(SDL_VIDEODRIVER=dummy "$EMU" roms/acid2/dmg-acid2.gb --ui-keys "space,space" --frames 20 2>&1)"
expect "and resumes"   "resumed" "$OUT"

# Paused from the start, nothing is emulated at all.
OUT="$(SDL_VIDEODRIVER=dummy "$EMU" roms/acid2/dmg-acid2.gb --paused --frames 20 2>&1)"
expect "a paused emulator emulates nothing" "emulated time    : 0.000 s" "$OUT"

# Reset restarts the loaded cartridge.
OUT="$(SDL_VIDEODRIVER=dummy "$EMU" roms/acid2/dmg-acid2.gb --ui-keys "r" --frames 20 2>&1)"
expect "reset restarts the cartridge" "reset" "$OUT"

# --- The interface is actually drawn -----------------------------------------
echo
echo "== the interface is drawn =="

SDL_VIDEODRIVER=dummy "$EMU" roms/acid2/dmg-acid2.gb --frames 12 --out "$TMP/gui.ppm" --quiet >/dev/null 2>&1
HEADER="$(head -c 15 "$TMP/gui.ppm" | tr '\n' ' ')"
expect "the window is taller than the screen, for the bar" "P6 640 616" "$HEADER"

# The bar must not be a copy of the game: compare the last rows against the
# screen's own colours. A distinct set of bytes means something else is there.
SCREEN_BYTES=$(tail -c +16 "$TMP/gui.ppm" | head -c $((640 * 576 * 3)) | od -An -tu1 -v | tr ' ' '\n' | grep . | sort -u | wc -l)
BAR_BYTES=$(tail -c $((640 * 40 * 3)) "$TMP/gui.ppm" | od -An -tu1 -v | tr ' ' '\n' | grep . | sort -u | wc -l)
if [ "$BAR_BYTES" -gt 4 ] && [ "$BAR_BYTES" -ne "$SCREEN_BYTES" ]; then
    printf '  \033[1;32mPASS\033[0m  the bar is drawn below the screen (%s distinct values, screen has %s)\n' \
           "$BAR_BYTES" "$SCREEN_BYTES"
    pass=$((pass + 1))
else
    printf '  \033[1;31mFAIL\033[0m  the bar area looks like the screen (%s values)\n' "$BAR_BYTES"
    fail=$((fail + 1))
fi

SDL_VIDEODRIVER=dummy "$EMU" --browser --frames 3 --out "$TMP/browser.ppm" --quiet >/dev/null 2>&1
if cmp -s "$TMP/gui.ppm" "$TMP/browser.ppm"; then
    printf '  \033[1;31mFAIL\033[0m  the browser draws the same thing as the emulator\n'; fail=$((fail + 1))
else
    printf '  \033[1;32mPASS\033[0m  the browser draws its own screen\n'; pass=$((pass + 1))
fi

# --- Errors ------------------------------------------------------------------
echo
echo "== errors =="

OUT="$(SDL_VIDEODRIVER=dummy "$EMU" /no/such/cartridge.gb --frames 5 2>&1)"
expect "a missing cartridge is reported" "cannot open" "$OUT"

OUT="$(SDL_VIDEODRIVER=dummy "$EMU" roms/acid2/dmg-acid2.gb --ui-keys "nonsense" --frames 5 2>&1)"
expect "an unknown scripted key is reported" "unknown key name" "$OUT"

# --- Built-in checks ----------------------------------------------------------
echo
echo "== built-in interface checks =="
if "$EMU" --cpucheck --quiet >/dev/null 2>&1; then
    printf '  \033[1;32mPASS\033[0m  13 interface checks inside --cpucheck\n'; pass=$((pass + 1))
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
