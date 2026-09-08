#!/usr/bin/env bash
#
# Run clang-tidy over the .cpp files a push touches and fail on findings.
#
# clang-tidy exits 0 for a plain warning, so --warnings-as-errors is what makes
# a check binding. Exit code is the whole signal here: clang-tidy also writes an
# unconditional "N warnings generated." tally to stderr, so deciding pass/fail by
# looking at output rather than status is how pocc/pre-commit-hooks v1.4.0 gets
# this wrong (its filter matches the singular "warning generated" only).
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

status=0
checked=0
for file in "$@"; do
    case "$file" in
    *.cpp) ;;
    *) continue ;;  # headers are analysed through the TUs that include them
    esac

    # A file added since the last configure has no database entry, and clang-tidy
    # would guess at the flags rather than say so. pre-commit passes repo-relative
    # paths while the database records absolute ones, so match on the suffix.
    if ! grep -q "/$file\"" "$DB" 2>/dev/null; then
        echo "skipping $file: no compile_commands.json entry, run 'make debug'"
        continue
    fi

    # The tally counts compiler warnings in the TU, not findings, and reads as a
    # contradiction next to a passing hook. Dropping it is safe because pass/fail
    # is the exit code here, never the output.
    checked=$((checked + 1))
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
    echo "clang-tidy found problems in the enforced tier." >&2
    echo "Fix them, or add a NOLINT with a reason if it is a false positive." >&2
fi

[ "$checked" -gt 0 ] && echo "clang-tidy: $checked file(s) checked"
exit "$status"
