#!/bin/bash
# Clean unused includes from C++ source files
# Uses clangd diagnostics via clang-tidy

set -e

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJECT_ROOT"

# Check for required tools
if ! command -v clang-tidy &> /dev/null; then
    echo "Error: clang-tidy not found. Install with: brew install llvm"
    exit 1
fi

# Find all C++ source and header files in magda/ directory
echo "Finding C++ files in magda/..."
FILES=$(find magda -name "*.cpp" -o -name "*.hpp" | grep -v "build" | sort)

if [ -z "$FILES" ]; then
    echo "No C++ files found!"
    exit 1
fi

FILE_COUNT=$(echo "$FILES" | wc -l | tr -d ' ')
echo "Found $FILE_COUNT files to check"
echo ""

# Run clang-tidy with misc-include-cleaner check
FIXED_COUNT=0
TOTAL_COUNT=0

for file in $FILES; do
    TOTAL_COUNT=$((TOTAL_COUNT + 1))
    echo "[$TOTAL_COUNT/$FILE_COUNT] Checking: $file"

    # Run clang-tidy with auto-fix for unused includes
    # Using misc-include-cleaner which detects unused headers
    if clang-tidy \
        -p cmake-build-debug \
        --checks="misc-include-cleaner" \
        --fix \
        --format-style=file \
        "$file" 2>&1 | grep -q "warning:"; then
        FIXED_COUNT=$((FIXED_COUNT + 1))
        echo "  ✓ Fixed unused includes"
    fi
done

echo ""
echo "======================="
echo "Cleanup complete!"
echo "Files checked: $FILE_COUNT"
echo "Files modified: $FIXED_COUNT"
echo "======================="
