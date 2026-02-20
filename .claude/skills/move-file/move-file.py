#!/usr/bin/env python3
"""
Move a C++ source file and update all #include references throughout the project.

Usage (run from project root):
    python3 .claude/skills/move-file/move-file.py <old-path> <new-path>

Both paths must be relative to the project root.
"""

import os
import re
import sys
import subprocess
from pathlib import Path


def find_project_root() -> Path:
    """Find project root by looking for the top-level CMakeLists.txt."""
    cwd = Path.cwd()
    if (cwd / "CMakeLists.txt").exists():
        return cwd
    path = Path(__file__).resolve().parent
    while path != path.parent:
        if (path / "CMakeLists.txt").exists():
            return path
        path = path.parent
    raise RuntimeError("Could not find project root (no CMakeLists.txt found)")


def git_mv(old_abs: Path, new_abs: Path) -> None:
    """Use git mv to move the file (preserves git history)."""
    new_abs.parent.mkdir(parents=True, exist_ok=True)
    result = subprocess.run(
        ["git", "mv", str(old_abs), str(new_abs)],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        print(f"Error: git mv failed:\n{result.stderr}", file=sys.stderr)
        sys.exit(1)


def find_cpp_files(root: Path) -> list:
    """Find all .cpp and .hpp files under the project root, excluding build dirs."""
    files = []
    for ext in ("*.cpp", "*.hpp"):
        files.extend(root.rglob(ext))
    excluded = {"cmake-build-debug", "cmake-build-release", "third_party", "build"}
    return [f for f in files if not any(part in excluded for part in f.parts)]


def resolve_include(include_str: str, from_file: Path, project_root: Path) -> Path:
    """
    Resolve an #include "..." path to an absolute path (no existence check).

    Handles two styles:
      - Project-root-relative: "magda/daw/core/Foo.hpp"
      - Relative: "../core/Foo.hpp", "Foo.hpp", "subdir/Foo.hpp"
    """
    if include_str.startswith("magda/"):
        return (project_root / include_str).resolve()
    return (from_file.parent / include_str).resolve()


def compute_relative_include(from_file: Path, to_file: Path) -> str:
    """Compute the relative include path from from_file's directory to to_file."""
    rel = os.path.relpath(str(to_file), str(from_file.parent))
    return rel.replace("\\", "/")


def update_includes_in_file(
    file_path: Path,
    old_abs: Path,
    new_abs: Path,
    project_root: Path,
) -> bool:
    """
    Update all #include "..." lines in file_path that resolve to old_abs.
    Rewrites them to point to new_abs, preserving the original include style.
    Returns True if any changes were made.
    """
    try:
        content = file_path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return False

    old_resolved = old_abs.resolve()
    lines = content.split("\n")
    changed = False
    new_lines = []

    for line in lines:
        match = re.match(r'^(\s*#include\s+")([^"]+)(".*)', line)
        if match:
            prefix, include_str, suffix = match.groups()
            resolved = resolve_include(include_str, file_path, project_root)
            if resolved == old_resolved:
                if include_str.startswith("magda/"):
                    # Preserve project-root-relative style
                    new_include = str(new_abs.relative_to(project_root)).replace("\\", "/")
                else:
                    # Preserve relative style
                    new_include = compute_relative_include(file_path, new_abs)
                new_lines.append(f"{prefix}{new_include}{suffix}")
                changed = True
                continue
        new_lines.append(line)

    if changed:
        file_path.write_text("\n".join(new_lines), encoding="utf-8")
    return changed


def update_own_includes(
    moved_file: Path,
    old_abs: Path,
    new_abs: Path,
    project_root: Path,
) -> bool:
    """
    Update relative includes *within* the moved file itself.

    When a file moves to a new directory, its relative includes to other files
    must be recomputed from the new location. Project-root-relative includes
    ("magda/daw/...") are unchanged since they don't depend on file location.
    Returns True if any changes were made.
    """
    try:
        content = moved_file.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return False

    lines = content.split("\n")
    changed = False
    new_lines = []

    for line in lines:
        match = re.match(r'^(\s*#include\s+")([^"]+)(".*)', line)
        if match:
            prefix, include_str, suffix = match.groups()
            # Project-root-relative includes don't depend on file location
            if include_str.startswith("magda/"):
                new_lines.append(line)
                continue
            # Resolve the included file from the OLD location using path math
            old_included = (old_abs.parent / include_str).resolve()
            # Compute what the path looks like from the NEW location
            new_include = compute_relative_include(new_abs, old_included)
            if new_include != include_str:
                new_lines.append(f"{prefix}{new_include}{suffix}")
                changed = True
                continue
        new_lines.append(line)

    if changed:
        moved_file.write_text("\n".join(new_lines), encoding="utf-8")
    return changed


def update_cmake(cmake_path: Path, old_rel: str, new_rel: str) -> bool:
    """
    Update source entries in CMakeLists.txt.
    old_rel / new_rel are paths relative to the CMakeLists.txt directory.
    Returns True if a replacement was made.
    """
    try:
        content = cmake_path.read_text(encoding="utf-8")
    except OSError:
        return False

    new_content = content.replace(old_rel, new_rel)
    if new_content != content:
        cmake_path.write_text(new_content, encoding="utf-8")
        return True
    return False


def main() -> None:
    if len(sys.argv) != 3:
        print("Usage: move-file.py <old-path> <new-path>", file=sys.stderr)
        print("  Paths are relative to the project root.", file=sys.stderr)
        sys.exit(1)

    project_root = find_project_root()
    old_path = Path(sys.argv[1])
    new_path = Path(sys.argv[2])
    old_abs = (project_root / old_path).resolve()
    new_abs = (project_root / new_path).resolve()

    if not old_abs.exists():
        print(f"Error: '{old_path}' does not exist.", file=sys.stderr)
        sys.exit(1)
    if new_abs.exists():
        print(f"Error: '{new_path}' already exists.", file=sys.stderr)
        sys.exit(1)

    print(f"Moving {old_path} → {new_path}")

    # Step 1: git mv
    git_mv(old_abs, new_abs)
    print("  ✓ git mv completed")

    # Collect all C++ files (new_abs is now the moved file)
    cpp_files = find_cpp_files(project_root)

    # Step 2: Update includes in all other files that reference the old path
    updated_files = []
    for file_path in cpp_files:
        if file_path.resolve() == new_abs.resolve():
            continue
        if update_includes_in_file(file_path, old_abs, new_abs, project_root):
            updated_files.append(str(file_path.relative_to(project_root)))

    if updated_files:
        print(f"  ✓ Updated includes in {len(updated_files)} file(s):")
        for f in sorted(updated_files):
            print(f"    - {f}")
    else:
        print("  ✓ No external files needed include updates")

    # Step 3: Update relative includes within the moved file itself
    if update_own_includes(new_abs, old_abs, new_abs, project_root):
        print(f"  ✓ Updated relative includes within {new_path}")
    else:
        print(f"  ✓ No internal includes needed updating in {new_path}")

    # Step 4: Update magda/daw/CMakeLists.txt for .cpp files
    if old_abs.suffix == ".cpp":
        cmake_path = project_root / "magda" / "daw" / "CMakeLists.txt"
        if cmake_path.exists():
            cmake_dir = cmake_path.parent
            try:
                old_cmake_rel = str(old_abs.relative_to(cmake_dir)).replace("\\", "/")
                new_cmake_rel = str(new_abs.relative_to(cmake_dir)).replace("\\", "/")
                if update_cmake(cmake_path, old_cmake_rel, new_cmake_rel):
                    print(f"  ✓ Updated CMakeLists.txt: {old_cmake_rel} → {new_cmake_rel}")
                else:
                    print(
                        f"  ⚠ '{old_cmake_rel}' not found in CMakeLists.txt (may need manual update)"
                    )
            except ValueError:
                print("  ⚠ File is outside magda/daw/; CMakeLists.txt not updated")

    print("\nDone! Review changes with: git diff")


if __name__ == "__main__":
    main()
