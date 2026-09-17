#!/usr/bin/env bash
# ===========================================================================
#  Step 3 regression test: bus dispatch and clock.
# ===========================================================================
#  Run from the repository root:
#      ./tests/run_bus_tests.sh
#
#  Everything is checked through --memtest, so this exercises the same code
#  path the CPU will use from step 4 onwards.
# ===========================================================================
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
EMU="${RETROEMU_BIN:-$ROOT/build/retroemu}"
ROM="$ROOT/roms/mooneye/mbc1/ram_64kb.gb"
CGB_ROM="$ROOT/roms/acid2/cgb-acid2.gbc"

if [ ! -x "$EMU" ]; then
    echo "error: $EMU not found. Build first: cmake -B build && cmake --build build -j"
    exit 1
fi

cd "$ROOT" || exit 1

pass=0
fail=0

expect() {          # expect <description> <pattern> <output>
    local desc="$1" pattern="$2" output="$3"
    if printf '%s' "$output" | grep -qF -- "$pattern"; then
        printf '  \033[1;32mPASS\033[0m  %s\n' "$desc"
        pass=$((pass + 1))
    else
        printf '  \033[1;31mFAIL\033[0m  %s\n' "$desc"
        printf '        expected to find: %s\n' "$pattern"
        fail=$((fail + 1))
    fi
}

reject() {          # reject <description> <pattern> <output>
    local desc="$1" pattern="$2" output="$3"
    if printf '%s' "$output" | grep -qF -- "$pattern"; then
        printf '  \033[1;31mFAIL\033[0m  %s (found "%s")\n' "$desc" "$pattern"
        fail=$((fail + 1))
    else
        printf '  \033[1;32mPASS\033[0m  %s\n' "$desc"
        pass=$((pass + 1))
    fi
}

OUT="$("$EMU" --memtest "$ROM" 2>&1)"

echo "== address dispatch =="
expect "0x0000 routes to ROM bank 0"      "0x0000  ROM bank 0" "$OUT"
expect "0x4000 routes to ROM bank N"      "0x4000  ROM bank N" "$OUT"
expect "0x8000 routes to VRAM"            "0x8000  VRAM" "$OUT"
expect "0xA000 routes to external RAM"    "0xA000  external RAM" "$OUT"
expect "0xC000 routes to WRAM bank 0"     "0xC000  WRAM bank 0" "$OUT"
expect "0xD000 routes to WRAM bank N"     "0xD000  WRAM bank N" "$OUT"
expect "0xFE00 routes to OAM"             "0xFE00  OAM" "$OUT"
expect "0xFEA0 routes to the unusable region" "0xFEA0  unusable" "$OUT"
expect "0xFF40 routes to the I/O registers"   "0xFF40  I/O registers" "$OUT"
expect "0xFF80 routes to HRAM"            "0xFF80  HRAM" "$OUT"
expect "0xFFFF routes to the IE register" "0xFFFF  IE register" "$OUT"

echo
echo "== write protection =="
expect "ROM is read-only"                 "0x0000  ROM bank 0       0xFF   read-only" "$OUT"
expect "VRAM is writable"                 "0x8000  VRAM             0x00   writable" "$OUT"
expect "HRAM is writable"                 "0xFF80  HRAM             0x00   writable" "$OUT"
expect "the unusable region drops writes" "0xFEA0  unusable         0xFF   read-only" "$OUT"
expect "writes into ROM are counted as bank commands" "bank-switch command(s)" "$OUT"

echo
echo "== echo RAM =="
expect "0xC000 is mirrored at 0xE000"     "read 0x42 at 0xE000   mirrored" "$OUT"
expect "0xE001 is mirrored at 0xC001"     "read 0x99 at 0xC001   mirrored" "$OUT"
reject "no mirroring failure"             "NOT MIRRORED" "$OUT"

echo
echo "== clock (decision D8) =="
expect "every access costs exactly 4 T-cycles" "(exactly 4, as the hardware charges)" "$OUT"
expect "peek() does not advance the clock"     "(free, as the debugger needs)" "$OUT"
expect "normal speed keeps both domains equal" "t_cpu +40, t_sys +40" "$OUT"
expect "double speed halves the system domain" "t_cpu +40, t_sys +20" "$OUT"
expect "the PPU is fed from the system domain" "(in sync)" "$OUT"
reject "no unexpected clock result"            "(UNEXPECTED)" "$OUT"

echo
echo "== CGB model =="
CGB_OUT="$("$EMU" --memtest "$CGB_ROM" 2>&1)"
expect "a CGB-only cartridge selects CGB mode" "Model          : CGB" "$CGB_OUT"
DMG_OUT="$("$EMU" --memtest "$ROM" 2>&1)"
expect "a DMG cartridge selects DMG mode"      "Model          : DMG" "$DMG_OUT"
FORCED="$("$EMU" --cgb --memtest "$ROM" 2>&1)"
expect "--cgb forces CGB on a DMG cartridge"   "Model          : CGB" "$FORCED"

echo
echo "== error handling =="
"$EMU" --memtest /nonexistent.gb >/dev/null 2>&1
if [ $? -eq 1 ]; then
    printf '  \033[1;32mPASS\033[0m  missing ROM is rejected\n'; pass=$((pass + 1))
else
    printf '  \033[1;31mFAIL\033[0m  missing ROM was not rejected\n'; fail=$((fail + 1))
fi
"$EMU" --memtest >/dev/null 2>&1
if [ $? -eq 1 ]; then
    printf '  \033[1;32mPASS\033[0m  --memtest with no argument is rejected\n'; pass=$((pass + 1))
else
    printf '  \033[1;31mFAIL\033[0m  --memtest with no argument was not rejected\n'; fail=$((fail + 1))
fi

echo
if [ "$fail" -eq 0 ]; then
    printf '\033[1;32m%d passed, 0 failed\033[0m\n' "$pass"; exit 0
fi
printf '\033[1;31m%d passed, %d failed\033[0m\n' "$pass" "$fail"; exit 1
