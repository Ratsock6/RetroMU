#!/usr/bin/env bash
# ===========================================================================
#  Step 13 regression test: memory bank controllers and battery saves.
# ===========================================================================
#  Section V.5 of the subject (p.8) requires MBC1, MBC2 and MBC5, and:
#
#    "You must also manage the in-game backup for games that propose this
#     feature (battery-backed cartridge RAM persisted on disk between
#     sessions)."
#
#  The four MBC ROMs of the bundle judge the controllers. Persistence is
#  checked literally: write, quit, start again, read back.
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

# --- The bundle's MBC ROMs ---------------------------------------------------
echo "== the subject's bundle =="

OUT="$("$EMU" --mooneye roms/mooneye/mbc1/ram_64kb.gb roms/mooneye/mbc1/rom_512kb.gb \
                        roms/mooneye/mbc2/ram.gb roms/mooneye/mbc5/rom_2Mb.gb \
                        --max-cycles 60000000 2>&1)"
expect "MBC1 bank switching"  "PASS  roms/mooneye/mbc1/rom_512kb.gb" "$OUT"
expect "MBC1 with RAM"        "PASS  roms/mooneye/mbc1/ram_64kb.gb"  "$OUT"
expect "MBC2"                 "PASS  roms/mooneye/mbc2/ram.gb"       "$OUT"
expect "MBC5 bank switching"  "PASS  roms/mooneye/mbc5/rom_2Mb.gb"   "$OUT"

# Every DMG ROM of the bundle, together.
OUT="$("$EMU" --mooneye roms/mooneye/acceptance/*.gb roms/mooneye/acceptance/oam_dma/*.gb \
                        roms/mooneye/mbc1/*.gb roms/mooneye/mbc2/*.gb roms/mooneye/mbc5/*.gb \
                        --max-cycles 60000000 2>&1)"
expect "all seven mooneye ROMs pass together" "7 passed, 0 failed" "$OUT"

# --- Battery saves, across sessions -----------------------------------------
echo
echo "== the save survives between sessions =="

cp roms/mooneye/mbc1/ram_64kb.gb "$TMP/battery.gb"

# Session one: open the RAM, write two bytes, quit.
OUT="$(printf 'w $0000 $0A\nw $A000 $42\nw $A001 $99\nq\n' | "$EMU" --debug "$TMP/battery.gb" 2>&1)"
expect "quitting writes the save" "saved $TMP/battery.sav" "$OUT"

if [ -f "$TMP/battery.sav" ]; then
    SIZE="$(wc -c < "$TMP/battery.sav")"
    if [ "$SIZE" -eq 8192 ]; then
        printf '  \033[1;32mPASS\033[0m  the save is the size of the cartridge RAM (8192 bytes)\n'
        pass=$((pass + 1))
    else
        printf '  \033[1;31mFAIL\033[0m  the save is %s bytes, expected 8192\n' "$SIZE"; fail=$((fail + 1))
    fi
else
    printf '  \033[1;31mFAIL\033[0m  no save file was written\n'; fail=$((fail + 1))
fi

# Session two: a fresh process must find the data where it was left.
OUT="$(printf 'w $0000 $0A\nm $A000 4\nq\n' | "$EMU" --debug "$TMP/battery.gb" 2>&1)"
expect "a new session reads the data back" '$A000  42 99' "$OUT"

# A cartridge without a battery must leave nothing behind.
cp roms/acid2/dmg-acid2.gb "$TMP/nobattery.gb"
printf 'q\n' | "$EMU" --debug "$TMP/nobattery.gb" >/dev/null 2>&1
if [ -f "$TMP/nobattery.sav" ]; then
    printf '  \033[1;31mFAIL\033[0m  a cartridge with no battery left a save file\n'; fail=$((fail + 1))
else
    printf '  \033[1;32mPASS\033[0m  a cartridge with no battery leaves nothing behind\n'; pass=$((pass + 1))
fi

# Nor should one that was merely looked at.
cp roms/mooneye/mbc1/ram_64kb.gb "$TMP/untouched.gb"
printf 'q\n' | "$EMU" --debug "$TMP/untouched.gb" >/dev/null 2>&1
if [ -f "$TMP/untouched.sav" ]; then
    printf '  \033[1;31mFAIL\033[0m  an untouched cartridge left a save file\n'; fail=$((fail + 1))
else
    printf '  \033[1;32mPASS\033[0m  nothing written means nothing saved\n'; pass=$((pass + 1))
fi

# The frontend saves too, not only the debugger.
cp roms/mooneye/mbc1/ram_64kb.gb "$TMP/front.gb"
OUT="$(SDL_VIDEODRIVER=dummy "$EMU" "$TMP/front.gb" --frames 400 2>&1)"
if [ -f "$TMP/front.sav" ]; then
    printf '  \033[1;32mPASS\033[0m  the window saves on exit as well\n'; pass=$((pass + 1))
else
    printf '  \033[1;31mFAIL\033[0m  the window did not save on exit\n'; fail=$((fail + 1))
fi

# --- The header still reads correctly ----------------------------------------
echo
echo "== cartridge reporting =="

OUT="$("$EMU" --list roms/mooneye/mbc1/ram_64kb.gb roms/mooneye/mbc2/ram.gb \
                     roms/mooneye/mbc5/rom_2Mb.gb 2>&1)"
expect "MBC1 with battery is reported"  "MBC1+RAM+BATTERY" "$OUT"
expect "MBC2 with battery is reported"  "MBC2+BATTERY"     "$OUT"
expect "MBC5 is reported"               "MBC5"             "$OUT"

# --- Nothing regressed --------------------------------------------------------
echo
echo "== nothing regressed =="
"$EMU" --screenshot roms/acid2/dmg-acid2.gb --out "$TMP/acid2.ppm" --max-cycles 30000000 --quiet
if cmp -s "$TMP/acid2.ppm" "$ROOT/tests/expected/dmg-acid2-reference.ppm"; then
    printf '  \033[1;32mPASS\033[0m  dmg-acid2 still matches the reference pixel for pixel\n'
    pass=$((pass + 1))
else
    printf '  \033[1;31mFAIL\033[0m  dmg-acid2 no longer matches the reference\n'; fail=$((fail + 1))
fi

# --- Built-in checks ----------------------------------------------------------
echo
echo "== built-in controller checks =="
if "$EMU" --cpucheck --quiet >/dev/null 2>&1; then
    printf '  \033[1;32mPASS\033[0m  23 controller checks inside --cpucheck\n'; pass=$((pass + 1))
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
