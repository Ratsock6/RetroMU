#!/usr/bin/env bash
# ===========================================================================
#  Step 5 regression test: disassembler and debugger.
# ===========================================================================
#  Checks the four capabilities section V.1 and V.2 of the subject require
#  (registers, next instruction, single step, one frame / one second), plus
#  the disassembler's agreement with the CPU.
#
#  The debugger reads commands from stdin, so it is driven from a here-doc.
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

expect() {          # expect <description> <pattern> <output>
    if printf '%s' "$3" | grep -qF -- "$2"; then
        printf '  \033[1;32mPASS\033[0m  %s\n' "$1"; pass=$((pass + 1))
    else
        printf '  \033[1;31mFAIL\033[0m  %s\n        expected to find: %s\n' "$1" "$2"
        fail=$((fail + 1))
    fi
}

# --- Subject V.1: registers, next instruction, single step -----------------
echo "== subject V.1: the three required debugger commands =="

OUT="$(printf 'r\nq\n' | "$EMU" --debug "$ROM" 2>&1)"
expect "registers are displayed"            "AF 01B0   BC 0013   DE 00D8   HL 014D" "$OUT"
expect "stack pointer and program counter"  "SP FFFE   PC 0100" "$OUT"
expect "flags are displayed"                "flags Z-HC" "$OUT"
expect "interrupt state is displayed"       "IME off" "$OUT"
expect "the next instruction is marked"     "-> \$0100   00         NOP" "$OUT"

OUT="$(printf 's\nq\n' | "$EMU" --debug "$ROM" 2>&1)"
expect "a single instruction executes"      "\$0100   00         NOP" "$OUT"
expect "PC advanced past it"                "PC 0101" "$OUT"

OUT="$(printf 's 3\nq\n' | "$EMU" --debug "$ROM" 2>&1)"
expect "stepping follows a jump"            "JP \$0150" "$OUT"
expect "and lands on the target"            "\$0150   F3         DI" "$OUT"

# --- Subject V.2: one frame, one second ------------------------------------
echo
echo "== subject V.2: one frame and one second =="

OUT="$(printf 'f\ni\nq\n' | "$EMU" --debug "$ROM" 2>&1)"
expect "one frame runs"                     "ran 1 frame(s)" "$OUT"
expect "the clock advanced by a frame"      "frames       1.0" "$OUT"

OUT="$(printf 't\ni\nq\n' | "$EMU" --debug "$ROM" 2>&1)"
expect "one second of emulation runs"       "ran 1 second(s)" "$OUT"
expect "roughly 60 frames elapsed"          "frames       59." "$OUT"

# --- Disassembly listing ----------------------------------------------------
echo
echo "== disassembly =="

OUT="$(printf 'd $0100 3\nq\n' | "$EMU" --debug "$ROM" 2>&1)"
expect "listing shows raw bytes"            "C3 50 01   JP \$0150" "$OUT"
expect "listing advances by instruction"    "\$0104" "$OUT"

# --- Convenience commands (not required by the subject) --------------------
echo
echo "== breakpoints and memory =="

OUT="$(printf 'b $0150\nc\nq\n' | "$EMU" --debug "$ROM" 2>&1)"
expect "a breakpoint is hit"                "breakpoint hit" "$OUT"
expect "execution stopped on it"            "PC 0150" "$OUT"

OUT="$(printf 'm $0100 16\nq\n' | "$EMU" --debug "$ROM" 2>&1)"
expect "memory dumps in hex and ascii"      "\$0100  00 C3 50 01" "$OUT"

OUT="$(printf 'w $C000 $42\nm $C000 16\nq\n' | "$EMU" --debug "$ROM" 2>&1)"
expect "a poke is visible in the dump"      "\$C000  42" "$OUT"

OUT="$(printf 'nonsense\nq\n' | "$EMU" --debug "$ROM" 2>&1)"
expect "an unknown command is reported"     "unknown command" "$OUT"

# --- Disassembler versus CPU ------------------------------------------------
echo
echo "== disassembler agrees with the CPU =="

if "$EMU" --cpucheck --quiet >/dev/null 2>&1; then
    printf '  \033[1;32mPASS\033[0m  built-in checks (opcode lengths and printed text)\n'
    pass=$((pass + 1))
else
    printf '  \033[1;31mFAIL\033[0m  built-in checks\n'; fail=$((fail + 1))
fi

ROMS=$(find roms -name '*.gb' -o -name '*.gbc' | sort)
# shellcheck disable=SC2086
OUT="$("$EMU" --discheck $ROMS --max-cycles 40000000 2>&1)"
expect "no mismatch across the bundled ROMs" "agrees with the CPU everywhere" "$OUT"

echo
if [ "$fail" -eq 0 ]; then
    printf '\033[1;32m%d passed, 0 failed\033[0m\n' "$pass"; exit 0
fi
printf '\033[1;31m%d passed, %d failed\033[0m\n' "$pass" "$fail"; exit 1
