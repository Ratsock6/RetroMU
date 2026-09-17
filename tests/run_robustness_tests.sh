#!/usr/bin/env bash
# ===========================================================================
#  Step 15: robustness. What happens when the input is WRONG.
# ===========================================================================
#  Every other suite checks that the emulator is right when the input is
#  right. This one checks that it stays standing when it is not: a truncated
#  cartridge, a directory where a file was expected, a save from another game,
#  a typo in a command-line number.
#
#  The subject's reason for caring is explicit (p.10):
#
#      "The bonus part will only be assessed if the mandatory part is
#       PERFECT. Perfect means the mandatory part has been integrally done
#       and works without malfunctioning."
#
#  A crash in front of a corrector is a malfunction whatever caused it, so the
#  rule enforced here is simple: BAD INPUT IS REPORTED AND REFUSED, NEVER
#  CRASHED ON. Exit code 1 and a message on stderr; never a signal, never an
#  uncaught exception, never a sanitizer report.
#
#  Two real bugs were found by writing it:
#    - a directory opens like a file and reports a size of LONG_MAX, so the
#      loader asked for an 8-exabyte allocation and died on std::bad_alloc;
#    - `--frames abc` parsed as 0, which means "no limit", so a typo silently
#      did the opposite of what was asked.
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
ok() { printf '  \033[1;32mPASS\033[0m  %s\n' "$1"; pass=$((pass + 1)); }
ko() { printf '  \033[1;31mFAIL\033[0m  %s\n        %s\n' "$1" "$2"; fail=$((fail + 1)); }

# --- The core assertion -----------------------------------------------------
#  refused <description> <command...>
#  The command must exit with code 1 exactly. Anything above 128 is a signal
#  (segmentation fault, abort), 134 in particular being an uncaught exception,
#  and those are the failures this file exists to catch.
refused() {
    local what="$1"; shift
    local out rc
    out="$("$@" 2>&1)"; rc=$?
    if [ "$rc" -eq 1 ]; then
        ok "$what"
    elif [ "$rc" -gt 128 ]; then
        ko "$what" "killed by signal $((rc - 128)) (exit $rc) instead of refusing cleanly"
    elif [ "$rc" -eq 0 ]; then
        ko "$what" "accepted it (exit 0) instead of refusing"
    else
        ko "$what" "exit $rc, expected 1"
    fi
    printf '%s' "$out" > "$TMP/last_output"
}

# survives <description> <command...>   — must finish without a signal
survives() {
    local what="$1"; shift
    local rc
    "$@" >/dev/null 2>&1; rc=$?
    if [ "$rc" -le 128 ]; then
        ok "$what"
    else
        ko "$what" "killed by signal $((rc - 128)) (exit $rc)"
    fi
}

says() {            # says <description> <text that must appear in the last output>
    if grep -qF -- "$2" "$TMP/last_output"; then
        ok "$1"
    else
        ko "$1" "the message did not mention: $2"
    fi
}

# --- Fixtures ---------------------------------------------------------------
: > "$TMP/empty.gb"
head -c 10   /dev/urandom > "$TMP/tiny.gb"
head -c 300  /dev/urandom > "$TMP/short.gb"
cp roms/acid2/dmg-acid2.gb "$TMP/good.gb"
head -c 20000 "$TMP/good.gb" > "$TMP/truncated.gb"
mkdir -p "$TMP/adirectory.gb"
ln -sf /nowhere/nothing.gb "$TMP/dangling.gb"

# A header claiming a cartridge type, a ROM size and a RAM size that do not
# exist. The file is otherwise a valid cartridge.
cp "$TMP/good.gb" "$TMP/absurd.gb"
printf '\xFE' | dd of="$TMP/absurd.gb" bs=1 seek=327 conv=notrunc status=none   # 0x147 type
printf '\x08' | dd of="$TMP/absurd.gb" bs=1 seek=328 conv=notrunc status=none   # 0x148 ROM size
printf '\x07' | dd of="$TMP/absurd.gb" bs=1 seek=329 conv=notrunc status=none   # 0x149 RAM size

echo "== files that are not cartridges =="
refused "a file that does not exist"          "$EMU" --info "$TMP/nosuch.gb"
says    "and says it cannot open it"          "cannot open"
refused "an empty file"                       "$EMU" --info "$TMP/empty.gb"
says    "and says it is empty"                "is empty"
refused "a file too small to hold a header"   "$EMU" --info "$TMP/tiny.gb"
says    "and says how small it is"            "too small to hold a cartridge header"
refused "a file just under the header size"   "$EMU" --info "$TMP/short.gb"
refused "a DIRECTORY passed as a cartridge"   "$EMU" --info "$TMP/adirectory.gb"
says    "and says it is a directory"          "is a directory"
refused "a dangling symbolic link"            "$EMU" --info "$TMP/dangling.gb"
refused "a character device (/dev/null)"      "$EMU" --info /dev/null

echo
echo "== cartridges that are valid files but absurd contents =="
#  These are NOT refused: the header parses, so the emulator reports what it
#  found and runs anyway. Refusing them would be wrong — a corrector's ROM may
#  well have a bad checksum — but crashing on them would be worse.
survives "a truncated cartridge is described"  "$EMU" --info "$TMP/truncated.gb"
survives "and runs without crashing"           "$EMU" --screenshot "$TMP/truncated.gb" \
                                                     --out "$TMP/o.ppm" --max-cycles 2000000 --quiet
survives "an unknown controller is described"  "$EMU" --info "$TMP/absurd.gb"
survives "and runs without crashing"           "$EMU" --screenshot "$TMP/absurd.gb" \
                                                     --out "$TMP/o.ppm" --max-cycles 2000000 --quiet

OUT="$("$EMU" --info "$TMP/truncated.gb" 2>&1)"
if printf '%s' "$OUT" | grep -q "Warnings:"; then
    ok "a truncated cartridge is reported as such rather than silently accepted"
else
    ko "a truncated cartridge is reported" "no warning was printed"
fi

echo
echo "== save files =="
cp roms/mooneye/mbc1/ram_64kb.gb "$TMP/batt.gb"

head -c 5 /dev/urandom > "$TMP/batt.sav"
OUT="$("$EMU" --info "$TMP/batt.gb" 2>&1)"
if printf '%s' "$OUT" | grep -q "save file is 5 bytes"; then
    ok "a save file that is too short is loaded, and the mismatch is reported"
else
    ko "a short save file is reported" "no warning about the save size"
fi

head -c 200000 /dev/zero > "$TMP/batt.sav"
OUT="$("$EMU" --info "$TMP/batt.gb" 2>&1)"
if printf '%s' "$OUT" | grep -q "save file is 200000 bytes"; then
    ok "a save file that is too long is truncated, and the mismatch is reported"
else
    ko "a long save file is reported" "no warning about the save size"
fi

rm -rf "$TMP/batt.sav"; mkdir -p "$TMP/batt.sav"
survives "a DIRECTORY where the save file should be" "$EMU" --screenshot "$TMP/batt.gb" \
                                                     --out "$TMP/o.ppm" --max-cycles 1000000 --quiet
rm -rf "$TMP/batt.sav"

rm -f "$TMP/batt.sav"
OUT="$("$EMU" --info "$TMP/batt.gb" 2>&1)"
if printf '%s' "$OUT" | grep -q "save file is"; then
    ko "no save file is not a warning" "it complained about a save that does not exist"
else
    ok "a cartridge with no save file yet says nothing"
fi

echo
echo "== command line =="
refused "an unknown option"                   "$EMU" --nonsense
refused "two actions at once"                 "$EMU" --info --list "$TMP/good.gb"
says    "and names both of them"              "cannot be combined"
refused "an action with no file"              "$EMU" --info
refused "--out with no path"                  "$EMU" --screenshot "$TMP/good.gb" --out
refused "--frames with a word instead of a number" "$EMU" --frames abc --info "$TMP/good.gb"
says    "and says what it could not parse"    "is not a number"
refused "--scale with a word"                 "$EMU" --scale abc --info "$TMP/good.gb"
refused "--scale with a negative number"      "$EMU" --scale -1 --info "$TMP/good.gb"
refused "--scale out of range"                "$EMU" --scale 999 --info "$TMP/good.gb"
refused "--max-cycles in scientific notation" "$EMU" --max-cycles 1e9 --info "$TMP/good.gb"
refused "--max-cycles of zero"                "$EMU" --max-cycles 0 --info "$TMP/good.gb"
refused "--trace-limit of zero"               "$EMU" --trace "$TMP/good.gb" --trace-limit 0
refused "--tracediff with a single file"      "$EMU" --tracediff "$TMP/good.gb"

echo
echo "== output paths =="
refused "a screenshot into a directory that does not exist" \
        "$EMU" --screenshot "$TMP/good.gb" --out "$TMP/nodir/x.ppm" --max-cycles 1000000 --quiet
refused "a screenshot onto a directory" \
        "$EMU" --screenshot "$TMP/good.gb" --out "$TMP/adirectory.gb" --max-cycles 1000000 --quiet

echo
echo "== the debugger =="
#  The debugger is the tool you reach for when you already do not trust what
#  is happening, so it is the last place that may silently do something other
#  than what was typed.
DBG_OUT="$(printf 'zzz\ns -1\nd abc\nb xyz\nm\nw\nk zz\ns 2\nq\n' \
           | "$EMU" --debug "$TMP/good.gb" 2>&1)"
printf '%s' "$DBG_OUT" > "$TMP/last_output"

says "an unknown command is named"              "unknown command 'zzz'"
says "a negative step count is refused"         "'-1' is not a number"
says "a word where an address belongs is refused" "'abc' is not a number"
says "a breakpoint at a nonsense address is refused" "'xyz' is not a number"
says "a command missing its argument shows its usage" "usage: m <addr>"
says "an unknown button is named"               "unknown button 'zz'"

if printf '%s' "$DBG_OUT" | grep -q "PC 0150"; then
    ok "and the session carries on afterwards"
else
    ko "the session carries on after bad commands" "the debugger did not reach PC 0150"
fi

echo
echo "== the window =="
#  The GUI is what a corrector actually runs, so the same rule applies to it.
#  SDL's dummy video driver lets the window be exercised with no display.
export SDL_VIDEODRIVER=dummy

refused "the window refuses a cartridge that does not exist" \
        "$EMU" "$TMP/nosuch.gb" --frames 3 --quiet
says    "and points at the browser as a way out" "run with no argument"
refused "the window refuses a directory"  "$EMU" "$TMP/adirectory.gb" --frames 3 --quiet
refused "the window refuses an empty file" "$EMU" "$TMP/empty.gb" --frames 3 --quiet

survives "the window opens with no cartridge at all" "$EMU" --frames 3 --quiet
survives "the browser opens with no cartridge at all" "$EMU" --browser --frames 3 --quiet
survives "a truncated cartridge still runs in the window" \
         "$EMU" "$TMP/truncated.gb" --frames 3 --quiet
survives "an absurd header still runs in the window" \
         "$EMU" "$TMP/absurd.gb" --frames 3 --quiet

unset SDL_VIDEODRIVER

echo
echo "== repository hygiene =="
#  The subject forbids committing a commercial ROM (p.6, p.11), and a stray
#  .sav or a failed-test capture has no business in the repository either.
#  These patterns are checked rather than trusted because git does NOT strip a
#  trailing comment from a .gitignore line: `*.sav  # saves` is the literal
#  pattern `*.sav  # saves` and matches nothing. Five patterns here were dead
#  that way until step 15, and nothing said so.
if command -v git > /dev/null && [ -d .git ]; then
    for pattern in foo.sav foo.rtc foo.state foo.ppm trace9.log \
                   roms/acid2/dmg-acid2.sav acid2-failed.ppm; do
        if git check-ignore -q "$pattern"; then
            ok "$pattern is ignored"
        else
            ko "$pattern is ignored" "it would be committed by 'git add -A'"
        fi
    done
    for kept in tests/expected/dmg-acid2-reference.ppm tests/expected/cgb-acid2-reference.ppm; do
        if git check-ignore -q "$kept"; then
            ko "$kept stays versioned" "the reference image is being ignored"
        else
            ok "$(basename "$kept") stays versioned"
        fi
    done
else
    ok "skipped: not a git checkout"
fi

echo
echo "== the good path still works =="
survives "a valid cartridge is still described" "$EMU" --info "$TMP/good.gb"
if "$EMU" --screenshot "$TMP/good.gb" --out "$TMP/good.ppm" --max-cycles 30000000 --quiet &&
   cmp -s "$TMP/good.ppm" tests/expected/dmg-acid2-reference.ppm; then
    ok "and still renders identically to the reference image"
else
    ko "the good path is unaffected" "dmg-acid2 no longer matches its reference"
fi

echo
if [ "$fail" -eq 0 ]; then
    printf '\033[1;32m%d passed, 0 failed\033[0m\n' "$pass"; exit 0
fi
printf '\033[1;31m%d passed, %d failed\033[0m\n' "$pass" "$fail"; exit 1
