#!/usr/bin/env bash
# ===========================================================================
#  Step 14 regression test: the Game Boy Color (subject V.6, p.8).
# ===========================================================================
#  The CGB is not a DMG with colours bolted on. Five things change shape:
#
#    - palettes become real 15-bit colours living in their own RAM, reached
#      through a one-byte window (BCPS/BCPD, OCPS/OCPD);
#    - video memory gains a second bank holding one ATTRIBUTE byte per map
#      cell: palette, bank, horizontal flip, vertical flip, priority;
#    - LCDC bit 0 stops meaning "draw the background" and starts meaning
#      "let the priority bits decide";
#    - sprites are ranked by their position in OAM instead of by X;
#    - the CPU can run twice as fast while the screen does not.
#
#  The judge is cgb-acid2, the colour counterpart of dmg-acid2. Every feature
#  above has a visible failure mode in it, so a wrong pixel points straight at
#  the rule behind it. tests/expected/cgb-acid2-reference.ppm is the author's
#  own reference image (MIT, Matt Currie, see the accompanying .LICENSE),
#  converted once from PNG to PPM with the expansion formula he documents:
#  (v << 3) | (v >> 2) per 5-bit channel. The comparison is then a plain byte
#  compare: no network, no image library, no Python.
# ===========================================================================
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
EMU="${RETROEMU_BIN:-$ROOT/build/retroemu}"
REFERENCE="$ROOT/tests/expected/cgb-acid2-reference.ppm"
DMG_REFERENCE="$ROOT/tests/expected/dmg-acid2-reference.ppm"

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

ok()   { printf '  \033[1;32mPASS\033[0m  %s\n' "$1"; pass=$((pass + 1)); }
ko()   { printf '  \033[1;31mFAIL\033[0m  %s\n        %s\n' "$1" "$2"; fail=$((fail + 1)); }

expect() {          # expect <description> <pattern> <output>
    if printf '%s' "$3" | strip_ansi | grep -qF -- "$2"; then
        ok "$1"
    else
        ko "$1" "expected to find: $2"
    fi
}

# Count the distinct colours in a PPM body (the 15-byte header is skipped).
colours() { tail -c +16 "$1" | od -An -tx1 -v -w3 | sort -u | grep -c .; }

# --- The decisive test ------------------------------------------------------
echo "== cgb-acid2 against the reference image =="

OUT="$("$EMU" --screenshot roms/acid2/cgb-acid2.gbc --out "$TMP/cgb.ppm" \
              --max-cycles 30000000 2>&1)"
expect "the ROM reaches its own end marker" "stopped : on the ROM's LD B,B marker" "$OUT"

if [ ! -f "$REFERENCE" ]; then
    ko "the reference image is present" "missing: $REFERENCE"
elif cmp -s "$TMP/cgb.ppm" "$REFERENCE"; then
    ok "the rendered screen is identical to the reference, pixel for pixel"
else
    ko "the rendered screen matches the reference" "it differs"
    cmp "$TMP/cgb.ppm" "$REFERENCE" 2>&1 | sed 's/^/        /'
    cp "$TMP/cgb.ppm" "$ROOT/cgb-acid2-failed.ppm" 2>/dev/null
    printf '        kept %s so it can be opened and compared\n' "$ROOT/cgb-acid2-failed.ppm"
fi

# --- It really is in colour -------------------------------------------------
echo
echo "== the image really is in colour =="

N="$(colours "$TMP/cgb.ppm")"
if [ "$N" -gt 4 ]; then
    ok "the capture uses $N distinct colours, so it cannot be the DMG renderer"
else
    ko "the capture uses more than four colours" "only $N found: the DMG path ran"
fi

REF_N="$(colours "$REFERENCE")"
if [ "$N" -eq "$REF_N" ]; then
    ok "and exactly as many as the reference ($REF_N)"
else
    ko "the palette matches the reference" "$N colours against $REF_N"
fi

# --- Determinism ------------------------------------------------------------
echo
echo "== the same ROM always renders the same screen =="
"$EMU" --screenshot roms/acid2/cgb-acid2.gbc --out "$TMP/a.ppm" --max-cycles 30000000 --quiet
"$EMU" --screenshot roms/acid2/cgb-acid2.gbc --out "$TMP/b.ppm" --max-cycles 30000000 --quiet
if cmp -s "$TMP/a.ppm" "$TMP/b.ppm"; then
    ok "two runs produce the same image"
else
    ko "two runs produce the same image" "they differed"
fi

HEADER="$(head -c 15 "$TMP/a.ppm" | tr '\n' ' ')"
expect "the capture is a 160x144 PPM" "P6 160 144 255" "$HEADER"

# --- The model comes from the cartridge header ------------------------------
echo
echo "== choosing the machine =="

OUT="$("$EMU" --info roms/acid2/cgb-acid2.gbc 2>&1)"
expect "the header declares a CGB-only cartridge" "CGB only" "$OUT"

OUT="$("$EMU" --memtest roms/acid2/cgb-acid2.gbc 2>&1)"
expect "and the emulator boots as a CGB without being told to" "Model          : CGB" "$OUT"

OUT="$("$EMU" --memtest roms/acid2/dmg-acid2.gb 2>&1)"
expect "a black-and-white cartridge boots as a DMG" "Model          : DMG" "$OUT"

OUT="$("$EMU" --memtest roms/acid2/dmg-acid2.gb --cgb 2>&1)"
expect "unless --cgb forces the colour machine" "Model          : CGB" "$OUT"

# --- DMG compatibility mode -------------------------------------------------
echo
echo "== a colour console running a black-and-white cartridge =="
#  A CGB running a DMG cartridge keeps every piece of extra hardware but draws
#  in black and white. The proof is that the picture is byte-for-byte the one
#  the DMG produces.
"$EMU" --screenshot roms/acid2/dmg-acid2.gb --cgb --out "$TMP/compat.ppm" \
       --max-cycles 30000000 --quiet
if cmp -s "$TMP/compat.ppm" "$DMG_REFERENCE"; then
    ok "dmg-acid2 forced onto a CGB still matches the DMG reference exactly"
else
    ko "compatibility mode matches the DMG reference" "the picture changed"
fi

N="$(colours "$TMP/compat.ppm")"
if [ "$N" -le 4 ]; then
    ok "and still uses at most four shades ($N)"
else
    ko "compatibility mode stays in four shades" "$N colours found"
fi

# --- The two clock domains --------------------------------------------------
echo
echo "== double speed (KEY1, 0xFF4D) =="
#  This is what the clock was split in two for, back in step 3: the CPU and
#  the timer speed up, the screen does not.
OUT="$("$EMU" --memtest roms/acid2/cgb-acid2.gbc 2>&1)"
expect "at normal speed both clocks advance together" "t_cpu +40, t_sys +40" "$OUT"
expect "at double speed the PPU receives half the cycles" "t_cpu +40, t_sys +20" "$OUT"
expect "and the PPU stays in step with the system clock" "(in sync)" "$OUT"

# --- The tile viewer still works on a colour cartridge -----------------------
echo
echo "== tools =="
"$EMU" --tiles roms/acid2/cgb-acid2.gbc --out "$TMP/tiles.ppm" --max-cycles 20000000 --quiet
HEADER="$(head -c 15 "$TMP/tiles.ppm" | tr '\n' ' ')"
expect "the tile viewer writes a 128x192 sheet" "P6 128 192 255" "$HEADER"

# --- Built-in checks ---------------------------------------------------------
echo
echo "== built-in colour checks =="
if "$EMU" --cpucheck --quiet >/dev/null 2>&1; then
    ok "the CGB checks inside --cpucheck all pass"
else
    ko "--cpucheck" "see the failures below"
    "$EMU" --cpucheck | strip_ansi | grep -A2 FAIL | sed 's/^/        /'
fi

echo
if [ "$fail" -eq 0 ]; then
    printf '\033[1;32m%d passed, 0 failed\033[0m\n' "$pass"; exit 0
fi
printf '\033[1;31m%d passed, %d failed\033[0m\n' "$pass" "$fail"; exit 1
