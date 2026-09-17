#!/usr/bin/env bash
# ===========================================================================
#  Every test, one command.
# ===========================================================================
#      ./tests/run_all.sh              build if needed, then run every suite
#      ./tests/run_all.sh --sanitize   also rebuild with AddressSanitizer and
#                                      UndefinedBehaviorSanitizer and run the
#                                      whole thing again under them
#
#  The sanitized pass is the one that matters for an emulator: almost every
#  operation is an 8- or 16-bit wraparound or a shift, and UBSan is what tells
#  the difference between a wraparound that was intended and one that was not.
#  It is roughly ten times slower, which is why it is not the default.
# ===========================================================================
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT" || exit 1

SANITIZE=0
for arg in "$@"; do
    case "$arg" in
        --sanitize) SANITIZE=1 ;;
        -h|--help)
            sed -n '2,14p' "$0" | sed 's/^# \{0,1\}//'
            exit 0 ;;
        *) echo "unknown option: $arg"; exit 1 ;;
    esac
done

bold()  { printf '\033[1m%s\033[0m\n' "$1"; }
green() { printf '\033[1;32m%s\033[0m\n' "$1"; }
red()   { printf '\033[1;31m%s\033[0m\n' "$1"; }

# ---------------------------------------------------------------------------
#  run_suites <binary> <label>
# ---------------------------------------------------------------------------
suites_failed=0

run_suites() {
    local binary="$1" label="$2"
    local failed=0 total=0

    echo
    bold "=============================================================="
    bold "  $label"
    bold "=============================================================="

    for suite in "$ROOT"/tests/run_*.sh; do
        # Do not recurse into this file.
        case "$(basename "$suite")" in run_all.sh) continue ;; esac

        total=$((total + 1))
        printf '  %-30s' "$(basename "$suite")"

        local out rc
        out="$(RETROEMU_BIN="$binary" bash "$suite" 2>&1)"; rc=$?

        local summary
        summary="$(printf '%s' "$out" | sed 's/\x1b\[[0-9;]*m//g' | tail -1)"

        if [ "$rc" -eq 0 ]; then
            printf '\033[1;32mOK\033[0m    %s\n' "$summary"
        else
            printf '\033[1;31mFAILED\033[0m  (exit %d)\n' "$rc"
            printf '%s' "$out" | sed 's/\x1b\[[0-9;]*m//g' | grep -A3 'FAIL' | sed 's/^/        /'
            failed=$((failed + 1))
        fi
    done

    echo
    if [ "$failed" -eq 0 ]; then
        green "  $total/$total suites passed — $label"
    else
        red "  $failed of $total suites FAILED — $label"
        suites_failed=$((suites_failed + failed))
    fi
}

# ---------------------------------------------------------------------------
#  Release
# ---------------------------------------------------------------------------
CONFIG_LOG="$(mktemp)"
trap 'rm -f "$CONFIG_LOG"' EXIT

bold "Building (Release)"
# SDL's own configuration prints notes about optional system libraries it did
# not find. They are not ours and not problems, so they go to a log and are
# only shown if the configuration actually fails.
if ! cmake -B build -DCMAKE_BUILD_TYPE=Release -DRETROEMU_WARNINGS=ON > "$CONFIG_LOG" 2>&1; then
    red "cmake configuration failed:"; tail -30 "$CONFIG_LOG"; exit 1
fi
if ! cmake --build build -j > "$ROOT/build/last_build.log" 2>&1; then
    red "the build failed:"; tail -30 "$ROOT/build/last_build.log"; exit 1
fi

# A warning is not an error here, but it is worth seeing: it is what the
# corrector's compiler will print, and ours has been clean since step 15.
if grep -E "warning:" "$ROOT/build/last_build.log" | grep -v "_deps/" > /dev/null; then
    red "  the build produced warnings:"
    grep -E "warning:" "$ROOT/build/last_build.log" | grep -v "_deps/" | sed 's/^/    /'
    suites_failed=$((suites_failed + 1))
else
    green "  built with no warnings"
fi

run_suites "$ROOT/build/retroemu" "Release"

# ---------------------------------------------------------------------------
#  Sanitizers
# ---------------------------------------------------------------------------
if [ "$SANITIZE" -eq 1 ]; then
    echo
    bold "Building (Debug + AddressSanitizer + UndefinedBehaviorSanitizer)"
    if ! cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug \
                             -DRETROEMU_SANITIZE=ON \
                             -DRETROEMU_WARNINGS=ON > "$CONFIG_LOG" 2>&1; then
        red "cmake configuration failed:"; tail -30 "$CONFIG_LOG"; exit 1
    fi
    if ! cmake --build build-asan -j > "$ROOT/build-asan/last_build.log" 2>&1; then
        red "the sanitized build failed:"; tail -30 "$ROOT/build-asan/last_build.log"; exit 1
    fi
    green "  built"
    run_suites "$ROOT/build-asan/retroemu" "AddressSanitizer + UndefinedBehaviorSanitizer"
fi

# ---------------------------------------------------------------------------
echo
if [ "$suites_failed" -eq 0 ]; then
    green "=============================================================="
    green "  EVERYTHING PASSED"
    green "=============================================================="
    exit 0
fi
red "=============================================================="
red "  $suites_failed FAILURE(S)"
red "=============================================================="
exit 1
