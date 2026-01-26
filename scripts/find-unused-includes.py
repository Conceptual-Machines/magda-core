#!/usr/bin/env python3
"""
Simple heuristic to find potentially unused includes in C++ files.
Uses regex matching - may have false positives, so review changes!
"""

import re
import sys
from pathlib import Path
from typing import List, Tuple

def extract_includes(content: str) -> List[Tuple[int, str, str]]:
    """Extract includes with line numbers and full paths."""
    includes = []
    for i, line in enumerate(content.split('\n'), 1):
        match = re.match(r'^\s*#include\s+[<"]([^>"]+)[>"]', line)
        if match:
            includes.append((i, match.group(1), line))
    return includes

def get_symbols_from_include(include_path: str) -> List[str]:
    """Guess symbols that might come from an include file."""
    # Get filename without extension
    parts = Path(include_path).stem.split('_')

    # Generate potential class/namespace names
    symbols = []

    # CamelCase version
    camel = ''.join(p.capitalize() for p in parts)
    symbols.append(camel)

    # Add namespace versions
    if '/' in include_path:
        namespace = include_path.split('/')[0]
        symbols.append(namespace)

    # Add common patterns
    for part in parts:
        if len(part) > 2:  # Skip short parts
            symbols.append(part)
            symbols.append(part.capitalize())

    return symbols

def is_include_used(content: str, include_path: str, include_line: str) -> bool:
    """Heuristic check if an include is used."""

    # Skip own header (e.g., Foo.cpp includes Foo.hpp)
    if include_path.startswith('"'):
        return True  # Assume local includes are used

    # Skip system/standard library includes
    if not any(x in include_path for x in ['juce', 'tracktion', 'magda', '../']):
        return True

    # Skip the include line itself
    content_without_includes = '\n'.join(
        line for line in content.split('\n')
        if not line.strip().startswith('#include')
    )

    # Get potential symbols from include
    symbols = get_symbols_from_include(include_path)

    # Check if any symbol is referenced
    for symbol in symbols:
        if len(symbol) > 3:  # Only check meaningful symbols
            # Look for symbol usage (as type, namespace, function)
            patterns = [
                rf'\b{symbol}\b',  # Direct reference
                rf'{symbol}::',     # Namespace/class qualification
                rf'<{symbol}',      # Template parameter
            ]
            for pattern in patterns:
                if re.search(pattern, content_without_includes):
                    return True

    return False

def check_file(file_path: Path, auto_fix: bool = False) -> int:
    """Check a file for unused includes. Returns count of unused includes."""
    try:
        content = file_path.read_text()
    except Exception as e:
        print(f"Error reading {file_path}: {e}")
        return 0

    includes = extract_includes(content)
    unused = []

    for line_no, include_path, include_line in includes:
        if not is_include_used(content, include_path, include_line):
            unused.append((line_no, include_line))

    if unused:
        print(f"\n{file_path}:")
        for line_no, include_line in unused:
            print(f"  Line {line_no}: {include_line.strip()}")

        if auto_fix:
            # Remove unused includes
            lines = content.split('\n')
            for line_no, _ in reversed(unused):  # Reverse to maintain line numbers
                del lines[line_no - 1]

            # Write back
            file_path.write_text('\n'.join(lines))
            print(f"  → Removed {len(unused)} unused includes")

    return len(unused)

def main():
    auto_fix = '--fix' in sys.argv

    if auto_fix:
        print("⚠️  Auto-fix mode enabled - will modify files!")
        print()

    project_root = Path(__file__).parent.parent
    magda_dir = project_root / 'magda'

    if not magda_dir.exists():
        print(f"Error: {magda_dir} not found")
        return 1

    # Find all C++ files
    cpp_files = list(magda_dir.rglob('*.cpp'))
    hpp_files = list(magda_dir.rglob('*.hpp'))
    all_files = sorted(cpp_files + hpp_files)

    print(f"Checking {len(all_files)} files...")
    print("=" * 60)

    total_unused = 0
    files_with_unused = 0

    for file_path in all_files:
        unused_count = check_file(file_path, auto_fix)
        if unused_count > 0:
            total_unused += unused_count
            files_with_unused += 1

    print("\n" + "=" * 60)
    print(f"Summary:")
    print(f"  Files checked: {len(all_files)}")
    print(f"  Files with unused includes: {files_with_unused}")
    print(f"  Total unused includes: {total_unused}")

    if not auto_fix and total_unused > 0:
        print("\nRun with --fix to automatically remove unused includes")
        print("(Review changes carefully - this is heuristic based!)")

    return 0

if __name__ == '__main__':
    sys.exit(main())
