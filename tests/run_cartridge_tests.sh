#!/usr/bin/env bash
# ===========================================================================
#  Step 2 regression test: cartridge header parsing.
# ===========================================================================
#  Run from the repository root:
#      ./tests/run_cartridge_tests.sh
#
#  Two things are checked:
#    1. the summary of the 9 bundled test ROMs matches a golden file;
#    2. malformed input is rejected with a non-zero exit code and no crash.
#
#  Regenerate the golden file after an intentional format change:
#      ./tests/run_cartridge_tests.sh --update
# ===========================================================================
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
EMU="${RETROEMU_BIN:-$ROOT/build/retroemu}"
GOLDEN="$ROOT/tests/expected/cartridge_list.txt"
UPDATE=0
[ "${1:-}" = "--update" ] && UPDATE=1

if [ ! -x "$EMU" ]; then
    echo "error: $EMU not found. Build first: cmake -B build && cmake --build build -j"
    exit 1
fi

cd "$ROOT" || exit 1

pass=0
fail=0

check() {           # check <description> <expected-exit-code> <command...>
    local desc="$1" want="$2"; shift 2
    "$@" >/dev/null 2>&1
    local got=$?
    if [ "$got" -eq "$want" ]; then
        printf '  \033[1;32mPASS\033[0m  %s\n' "$desc"
        pass=$((pass + 1))
    else
        printf '  \033[1;31mFAIL\033[0m  %s (exit %d, expected %d)\n' "$desc" "$got" "$want"
        fail=$((fail + 1))
    fi
}

# --- 1. The nine bundled ROMs ---------------------------------------------
echo "== bundled test ROMs =="

mapfile -t ROMS < <(find roms -name '*.gb' -o -name '*.gbc' | sort)
if [ "${#ROMS[@]}" -ne 9 ]; then
    printf '  \033[1;31mFAIL\033[0m  expected 9 ROMs in roms/, found %d\n' "${#ROMS[@]}"
    fail=$((fail + 1))
fi

actual="$("$EMU" --list "${ROMS[@]}")"

if [ "$UPDATE" -eq 1 ]; then
    printf '%s\n' "$actual" > "$GOLDEN"
    echo "  golden file updated: $GOLDEN"
elif [ ! -f "$GOLDEN" ]; then
    printf '  \033[1;31mFAIL\033[0m  golden file missing, run with --update\n'
    fail=$((fail + 1))
elif [ "$actual" = "$(cat "$GOLDEN")" ]; then
    printf '  \033[1;32mPASS\033[0m  --list output matches the golden file\n'
    pass=$((pass + 1))
else
    printf '  \033[1;31mFAIL\033[0m  --list output differs from the golden file\n'
    diff -u "$GOLDEN" <(printf '%s\n' "$actual") | head -30
    fail=$((fail + 1))
fi

# Every bundled ROM must have a valid header checksum and parse cleanly.
for rom in "${ROMS[@]}"; do
    check "parses $rom" 0 "$EMU" --info "$rom"
done

# --- 2. Malformed input ----------------------------------------------------
echo
echo "== error handling =="

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

: > "$TMP/empty.gb"                                  # zero bytes
head -c 100 /dev/urandom > "$TMP/tiny.gb"            # shorter than the header
head -c 400 /dev/urandom > "$TMP/noise.gb"           # header-sized garbage

check "missing file is rejected"        1 "$EMU" --info "$TMP/nosuch.gb"
check "empty file is rejected"          1 "$EMU" --info "$TMP/empty.gb"
check "truncated file is rejected"      1 "$EMU" --info "$TMP/tiny.gb"
check "no argument is rejected"         1 "$EMU" --info
check "--list with no argument"         1 "$EMU" --list
check "one bad file among good ones"    1 "$EMU" --list roms/acid2/dmg-acid2.gb "$TMP/nosuch.gb"

# Garbage that is merely nonsensical must still parse without crashing:
# the header is reported, the checksum is flagged, the process exits 0.
check "random 400-byte file parses"     0 "$EMU" --info "$TMP/noise.gb"

# A ROM whose header checksum has been corrupted must still load, but warn.
cp roms/acid2/dmg-acid2.gb "$TMP/badsum.gb"
printf '\x00' | dd of="$TMP/badsum.gb" bs=1 seek=333 conv=notrunc status=none
if "$EMU" --info "$TMP/badsum.gb" 2>&1 | grep -q "MISMATCH"; then
    printf '  \033[1;32mPASS\033[0m  corrupted header checksum is reported\n'
    pass=$((pass + 1))
else
    printf '  \033[1;31mFAIL\033[0m  corrupted header checksum was not reported\n'
    fail=$((fail + 1))
fi

# --- Summary ---------------------------------------------------------------
echo
if [ "$fail" -eq 0 ]; then
    printf '\033[1;32m%d passed, 0 failed\033[0m\n' "$pass"
    exit 0
fi
printf '\033[1;31m%d passed, %d failed\033[0m\n' "$pass" "$fail"
exit 1
