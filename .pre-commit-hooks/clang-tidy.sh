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

# mapfile is bash 4. macOS still ships 3.2 as /bin/bash, where this would fail
# on a missing builtin rather than on anything to do with the code.
if [ "${BASH_VERSINFO[0]:-0}" -lt 4 ]; then
    echo "This hook needs bash 4 or newer; found ${BASH_VERSION:-unknown}." >&2
    echo "On macOS: brew install bash (Homebrew's comes first on PATH)." >&2
    exit 1
fi

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
# ninja's recorded dependencies. tus-for-headers.py applies the ceiling itself:
# the budget has to be spread across the changed headers, and only it knows
# which TU came from which header.
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

hook_dir=$(cd "$(dirname "$0")" && pwd)
resolver="$hook_dir/tus-for-headers.py"

# python3 then python, the fallback setup.sh accepts when it installs this hook.
# Hardcoding python3 meant a machine that only has `python`, which is a common
# Windows shape, installed the hook and then failed every C++ push on a missing
# interpreter. The resolver is always run through this, never its shebang.
if [ -n "${PYTHON:-}" ]; then
    :
elif command -v python3 >/dev/null 2>&1; then
    PYTHON=python3
elif command -v python >/dev/null 2>&1; then
    PYTHON=python
else
    echo "Neither python3 nor python found; cannot resolve translation units." >&2
    echo "Install Python, or set PYTHON=/path/to/python." >&2
    exit 1
fi

is_unbuilt_ok() {
    local candidate="$1" known
    for known in "${UNBUILT_OK[@]}"; do
        [ "$candidate" = "$known" ] && return 0
    done
    return 1
}

status=0
# Kept apart so the cap can never drop a file the push actually changed.
declare -a changed=()
declare -a expanded=()
declare -a headers=()

declare -a sources=()
for file in "$@"; do
    case "$file" in
    *.cpp) sources+=("$file") ;;
    *.h | *.hpp) headers+=("$file") ;;
    esac
done

# Database membership is resolved by the same code that reads it, rather than a
# grep here: the file records absolute native paths, so matching a relative one
# by string has to assume a separator, and on Windows that marks every file
# missing and passes the lot.
if [ ${#sources[@]} -gt 0 ]; then
    if ! absent=$($PYTHON "$resolver" --missing --build-dir "$BUILD_DIR" "${sources[@]}"); then
        echo "Could not read $DB." >&2
        exit 1
    fi
    for file in "${sources[@]}"; do
        if ! printf '%s\n' "$absent" | grep -qxF "$file"; then
            changed+=("$file")
        elif is_unbuilt_ok "$file"; then
            echo "note: $file is in no compile command, which is expected for it."
        else
            echo "$file has no compile_commands.json entry." >&2
            echo "Run 'make debug'. If it belongs to no target, say so in" >&2
            echo "UNBUILT_OK in $0 rather than leaving it unanalysed." >&2
            status=1
        fi
    done
fi

if [ ${#headers[@]} -gt 0 ]; then
    # Command substitution, not `mapfile < <(...)`: mapfile reports its own
    # status, so a process substitution's exit code is lost and a failing
    # mapping reads as success.
    if header_tus=$($PYTHON "$resolver" --build-dir "$BUILD_DIR" \
                      --max-tus "$MAX_TUS" "${headers[@]}"); then
        [ -n "$header_tus" ] && mapfile -t expanded <<<"$header_tus"
    else
        # Either the dependency data is unreadable or a header compiles into
        # nothing. Reporting clean off the back of either is the failure this
        # gate exists to prevent.
        echo "Could not map changed headers to translation units (see above)." >&2
        echo "Run 'make debug' so ninja has recorded their dependencies." >&2
        status=1
    fi
fi

if [ ${#changed[@]} -gt 0 ]; then
    mapfile -t changed < <(printf '%s\n' "${changed[@]}" | sort -u)
fi

# Header expansion only. A file the push actually changed is never dropped:
# capping the merged set meant a ninth changed .cpp went unanalysed and the gate
# still reported clean.
if [ ${#expanded[@]} -gt 0 ]; then
    if [ ${#changed[@]} -gt 0 ]; then
        mapfile -t expanded < <(
            printf '%s\n' "${expanded[@]}" | sort -u |
                grep -vxF -f <(printf '%s\n' "${changed[@]}") || true
        )
    else
        mapfile -t expanded < <(printf '%s\n' "${expanded[@]}" | sort -u)
    fi
fi

declare -a targets=("${changed[@]}" "${expanded[@]}")

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
