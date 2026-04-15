#!/bin/bash
# Pre-commit hook to enforce file size policy
# Warns at WARN_THRESHOLD LOC, fails at FAIL_THRESHOLD LOC

WARN_THRESHOLD=2500
FAIL_THRESHOLD=3500

# TODO: Files temporarily exempted pending decomposition
SKIP_FILES=(
    "magda/daw/ui/components/chain/DeviceSlotComponent.cpp"
)

exit_code=0
has_warnings=false

echo "Checking file sizes..."

for file in "$@"; do
    # Only check source files
    if [[ ! "$file" =~ \.(cpp|hpp|h|c|cc|cxx)$ ]]; then
        continue
    fi

    # Skip if file doesn't exist (deleted files)
    if [[ ! -f "$file" ]]; then
        continue
    fi

    # Skip temporarily exempted files
    skip=false
    for skip_file in "${SKIP_FILES[@]}"; do
        if [[ "$file" == *"$skip_file" ]]; then
            skip=true
            break
        fi
    done
    if [[ $skip == true ]]; then
        continue
    fi

    # Count lines of code (excluding empty lines and comments)
    loc=$(grep -v '^\s*$' "$file" | grep -v '^\s*//' | grep -v '^\s*/\*' | grep -v '^\s*\*' | wc -l | tr -d ' ')

    # Get actual line count for display
    total_lines=$(wc -l < "$file" | tr -d ' ')

    if [[ $loc -ge $FAIL_THRESHOLD ]]; then
        echo "❌ FAILED: $file has $loc LOC (threshold: $FAIL_THRESHOLD)"
        echo "   Please decompose this file into smaller, focused modules."
        exit_code=1
    elif [[ $loc -ge $WARN_THRESHOLD ]]; then
        echo "⚠️  WARNING: $file has $loc LOC (warning threshold: $WARN_THRESHOLD)"
        echo "   Consider decomposing this file to stay under 1500 LOC."
        has_warnings=true
    fi
done

if [[ $has_warnings == true && $exit_code == 0 ]]; then
    echo ""
    echo "Files with warnings are allowed to commit, but please address them soon."
fi

if [[ $exit_code != 0 ]]; then
    echo ""
    echo "❌ Commit blocked: Files exceed $FAIL_THRESHOLD LOC hard limit"
    echo "Please decompose large files before committing."
fi

exit $exit_code
