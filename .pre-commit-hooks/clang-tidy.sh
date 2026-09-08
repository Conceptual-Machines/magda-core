#!/usr/bin/env bash
#
# Run clang-tidy over what a push touches and fail on findings.
#
# clang-tidy exits 0 for a plain warning, so --warnings-as-errors is what makes
# a check binding. Exit code is the whole signal here: clang-tidy also writes an
# unconditional "N warnings generated." tally to stderr, so deciding pass/fail
# from output rather than status is how pocc/pre-commit-hooks v1.4.0 gets this
# wrong (its filter matches the singular "warning generated" only).
set -uo pipefail

# The bugprone/cert half of .clang-tidy, minus the five checks that still report
# findings. Sweeping those is what unblocks WarningsAsErrors in .clang-tidy;
# delete them from here as each reaches zero.
CHECKS='-*,bugprone-*'
CHECKS+=',-bugprone-easily-swappable-parameters,-bugprone-narrowing-conversions'
CHECKS+=',-bugprone-chained-comparison,-bugprone-suspicious-include'
CHECKS+=',-bugprone-exception-escape,-bugprone-unchecked-optional-access'
CHECKS+=',-bugprone-swapped-arguments,-bugprone-branch-clone,-bugprone-empty-catch'
CHECKS+=',-bugprone-signed-char-misuse,-bugprone-misplaced-widening-cast'
CHECKS+=',-bugprone-nondeterministic-pointer-iteration-order,-bugprone-macro-parentheses'
# Still to sweep: 31, 4 (deliberate pixel roundings), 2, 2 and 1 findings.
CHECKS+=',-bugprone-implicit-widening-of-multiplication-result,-bugprone-incorrect-roundings'
CHECKS+=',-bugprone-unused-return-value,-bugprone-return-const-ref-from-parameter'
CHECKS+=',-bugprone-reserved-identifier'
CHECKS+=',cert-oop54-cpp'

# Sources that are in the tree but in no compile command, so an absent database
# entry is expected rather than a stale build. Anything else missing is an error.
UNBUILT_OK=(
    # Built only when MAGDA_PRO_DEVICES is OFF, where the real pack replaces them.
    'magda/daw/device_packs/pro_stub/ProDevicePack.cpp'
    'magda/daw/device_packs/pro_stub/ProStubPlugin.cpp'
    # In no CMake target and referenced only by each other.
    'magda/daw/ui/components/automation/AutomationPointComponent.cpp'
    'magda/daw/ui/components/automation/BezierHandleComponent.cpp'
    'magda/daw/ui/components/automation/TensionHandleComponent.cpp'
)

# A header changed on its own still has to be analysed, and clang-tidy only
# analyses translation units, so headers map to the TUs that compile them via
# ninja's recorded dependencies. Capped because one widely included header
# reaches dozens of TUs at ~20s each and a pre-push hook has to finish; the cap
# is reported rather than applied quietly. 0 means no cap.
MAX_TUS="${CLANG_TIDY_MAX_TUS:-8}"

BUILD_DIR="${BUILD_DIR:-cmake-build-debug}"

# Homebrew's LLVM is newer than Apple's, so prefer it. Same lookup as the Makefile.
if [ -n "${CLANG_TIDY:-}" ]; then
    :
elif [ -x /opt/homebrew/opt/llvm/bin/clang-tidy ]; then
    CLANG_TIDY=/opt/homebrew/opt/llvm/bin/clang-tidy
else
    CLANG_TIDY=$(command -v clang-tidy 2>/dev/null || true)
fi

if [ -z "${CLANG_TIDY:-}" ]; then
    echo "clang-tidy not found on PATH or at /opt/homebrew/opt/llvm/bin." >&2
    echo "Install it (brew install llvm) or set CLANG_TIDY=/path/to/clang-tidy." >&2
    exit 1
fi

DB="$BUILD_DIR/compile_commands.json"
if [ ! -f "$DB" ]; then
    echo "$DB not found. Run 'make debug' first." >&2
    exit 1
fi

in_db() {
    # pre-commit passes repo-relative paths, the database records absolute ones.
    grep -q "/$1\"" "$DB" 2>/dev/null
}

is_unbuilt_ok() {
    local candidate="$1" known
    for known in "${UNBUILT_OK[@]}"; do
        [ "$candidate" = "$known" ] && return 0
    done
    return 1
}

status=0
declare -a targets=()
declare -a headers=()

for file in "$@"; do
    case "$file" in
    *.cpp)
        if in_db "$file"; then
            targets+=("$file")
        elif is_unbuilt_ok "$file"; then
            echo "note: $file is in no compile command, which is expected for it."
        else
            echo "$file has no compile_commands.json entry." >&2
            echo "Run 'make debug'. If it belongs to no target, say so in" >&2
            echo "UNBUILT_OK in $0 rather than leaving it unanalysed." >&2
            status=1
        fi
        ;;
    *.h | *.hpp)
        headers+=("$file")
        ;;
    esac
done

if [ ${#headers[@]} -gt 0 ]; then
    hook_dir=$(cd "$(dirname "$0")" && pwd)
    # Command substitution, not `mapfile < <(...)`: mapfile reports its own
    # status, so a process substitution's exit code is lost and a failing
    # mapping reads as success.
    if header_tus=$(BUILD_DIR="$BUILD_DIR" python3 "$hook_dir/tus-for-headers.py" "${headers[@]}"); then
        [ -n "$header_tus" ] && mapfile -t -O "${#targets[@]}" targets <<<"$header_tus"
    else
        # Either the dependency data is unreadable or a header compiles into
        # nothing. Reporting clean off the back of either is the failure this
        # gate exists to prevent.
        echo "Could not map changed headers to translation units (see above)." >&2
        echo "Run 'make debug' so ninja has recorded their dependencies." >&2
        status=1
    fi
fi

# One header can pull in a TU another already did, and so can a .cpp alongside
# its own header.
if [ ${#targets[@]} -gt 0 ]; then
    mapfile -t targets < <(printf '%s\n' "${targets[@]}" | sort -u)
fi

if [ "$MAX_TUS" -gt 0 ] && [ ${#targets[@]} -gt "$MAX_TUS" ]; then
    echo "note: ${#targets[@]} translation units affected, analysing the first $MAX_TUS."
    echo "      Set CLANG_TIDY_MAX_TUS=0 to analyse all of them."
    targets=("${targets[@]:0:$MAX_TUS}")
fi

for file in "${targets[@]:-}"; do
    [ -n "$file" ] || continue
    # The tally counts compiler warnings in the TU, not findings, and reads as a
    # contradiction next to a passing hook. Dropping it is safe because pass/fail
    # is the exit code here, never the output.
    if ! "$CLANG_TIDY" "$file" \
        --checks="$CHECKS" \
        --warnings-as-errors="$CHECKS" \
        --quiet \
        -p="$BUILD_DIR" \
        2> >(grep -Ev '^[0-9,]+ warnings? generated\.$' >&2); then
        status=1
    fi
done

if [ "$status" -ne 0 ]; then
    echo "" >&2
    echo "clang-tidy gate failed; see above." >&2
    echo "For a finding, fix it or add a NOLINT with a reason if it is wrong." >&2
fi

[ ${#targets[@]} -gt 0 ] && echo "clang-tidy: ${#targets[@]} translation unit(s) checked"
exit "$status"
