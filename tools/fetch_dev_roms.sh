#!/usr/bin/env bash
# ===========================================================================
#  Fetch the development test ROMs into roms-dev/ (gitignored).
# ===========================================================================
#  The bundle shipped with the subject contains no CPU test, so we use
#  Blargg's suite, which the subject names as legitimate test material
#  (Chapter II, p.4). These ROMs are NOT committed: roms-dev/ is gitignored,
#  and only the MIT bundle in roms/ is evaluated (Chapter VII, p.11).
#
#      ./tools/fetch_dev_roms.sh
# ===========================================================================
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$ROOT/roms-dev"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "Cloning blargg's test ROMs..."
git clone --depth 1 -q https://github.com/retrio/gb-test-roms "$TMP/gb-test-roms"

mkdir -p "$DEST"
for item in cpu_instrs instr_timing mem_timing halt_bug.gb; do
    cp -r "$TMP/gb-test-roms/$item" "$DEST/"
    echo "  $item"
done

echo
echo "Done. Run the CPU suite with:"
echo "    ./tests/run_cpu_tests.sh"
