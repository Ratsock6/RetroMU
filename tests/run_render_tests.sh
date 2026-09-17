#!/usr/bin/env bash
# ===========================================================================
#  Step 9 regression test: rendering.
# ===========================================================================
#  The decisive check is dmg-acid2. It is the canonical PPU test: it draws a
#  face, and every feature it exercises has a visible failure mode, so a
#  defect in the picture points straight at the bug behind it.
#
#  tests/expected/dmg-acid2-reference.ppm is the project's own reference image
#  (MIT, Matt Currie, see the accompanying .LICENSE), converted once from PNG
#  to PPM so the comparison is a plain byte compare: no network, no image
#  library, no Python.
# ===========================================================================
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
EMU="${RETROEMU_BIN:-$ROOT/build/retroemu}"
REFERENCE="$ROOT/tests/expected/dmg-acid2-reference.ppm"

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

# --- The decisive test ------------------------------------------------------
echo "== dmg-acid2 against the reference image =="

OUT="$("$EMU" --screenshot roms/acid2/dmg-acid2.gb --out "$TMP/acid2.ppm" \
              --max-cycles 30000000 2>&1)"
expect "the ROM reaches its own end marker" "stopped : on the ROM's LD B,B marker" "$OUT"

if [ ! -f "$REFERENCE" ]; then
    printf '  \033[1;31mFAIL\033[0m  reference image missing: %s\n' "$REFERENCE"
    fail=$((fail + 1))
elif cmp -s "$TMP/acid2.ppm" "$REFERENCE"; then
    printf '  \033[1;32mPASS\033[0m  the rendered screen is identical to the reference, pixel for pixel\n'
    pass=$((pass + 1))
else
    printf '  \033[1;31mFAIL\033[0m  the rendered screen differs from the reference\n'
    cmp "$TMP/acid2.ppm" "$REFERENCE" 2>&1 | sed 's/^/        /'
    printf '        keep %s and open it to see what is wrong\n' "$TMP/acid2.ppm"
    cp "$TMP/acid2.ppm" "$ROOT/acid2-failed.ppm" 2>/dev/null
    fail=$((fail + 1))
fi

# --- Determinism ------------------------------------------------------------
echo
echo "== the same ROM always renders the same screen =="
"$EMU" --screenshot roms/acid2/dmg-acid2.gb --out "$TMP/a.ppm" --max-cycles 30000000 --quiet
"$EMU" --screenshot roms/acid2/dmg-acid2.gb --out "$TMP/b.ppm" --max-cycles 30000000 --quiet
if cmp -s "$TMP/a.ppm" "$TMP/b.ppm"; then
    printf '  \033[1;32mPASS\033[0m  two runs produce the same image\n'; pass=$((pass + 1))
else
    printf '  \033[1;31mFAIL\033[0m  two runs produced different images\n'; fail=$((fail + 1))
fi

# --- The output is a well-formed image --------------------------------------
echo
echo "== output files =="

HEADER="$(head -c 15 "$TMP/a.ppm" | tr '\n' ' ')"
expect "the screenshot is a 160x144 PPM" "P6 160 144 255" "$HEADER"

SIZE="$(wc -c < "$TMP/a.ppm")"
if [ "$SIZE" -eq 69135 ]; then
    printf '  \033[1;32mPASS\033[0m  the file is exactly 160x144x3 bytes plus its header\n'; pass=$((pass + 1))
else
    printf '  \033[1;31mFAIL\033[0m  unexpected size: %s bytes\n' "$SIZE"; fail=$((fail + 1))
fi

# The tile viewer: before trusting a rendered screen it is worth looking at
# the building blocks themselves.
"$EMU" --tiles roms/acid2/dmg-acid2.gb --out "$TMP/tiles.ppm" --max-cycles 20000000 --quiet
HEADER="$(head -c 15 "$TMP/tiles.ppm" | tr '\n' ' ')"
expect "the tile viewer writes a 128x192 sheet" "P6 128 192 255" "$HEADER"

# --- Other bundled ROMs draw something ---------------------------------------
echo
echo "== the other bundled ROMs draw their own screens =="
for rom in roms/mooneye/acceptance/div_timing.gb roms/mooneye/mbc5/rom_2Mb.gb; do
    name="$(basename "$rom" .gb)"
    "$EMU" --screenshot "$rom" --out "$TMP/$name.ppm" --max-cycles 60000000 --quiet
    # A blank screen would be one single colour repeated. Count distinct bytes.
    SHADES="$(tail -c +16 "$TMP/$name.ppm" | od -An -tu1 -v | tr ' ' '\n' | grep -c . || true)"
    UNIQUE="$(tail -c +16 "$TMP/$name.ppm" | od -An -tu1 -v | tr ' ' '\n' | grep . | sort -u | wc -l)"
    if [ "$UNIQUE" -gt 1 ]; then
        printf '  \033[1;32mPASS\033[0m  %s renders something (%s distinct byte values)\n' "$name" "$UNIQUE"
        pass=$((pass + 1))
    else
        printf '  \033[1;31mFAIL\033[0m  %s rendered a blank screen\n' "$name"
        fail=$((fail + 1))
    fi
done

# --- Built-in checks ---------------------------------------------------------
echo
echo "== built-in rendering checks =="
if "$EMU" --cpucheck --quiet >/dev/null 2>&1; then
    printf '  \033[1;32mPASS\033[0m  13 rendering checks inside --cpucheck\n'; pass=$((pass + 1))
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
