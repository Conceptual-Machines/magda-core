#!/usr/bin/env python3
"""
Move a C++ source file and update all #include references throughout the project.

Usage (run from project root):
    python3 .claude/skills/move-file/move-file.py <old-path> <new-path>
    python3 .claude/skills/move-file/move-file.py --dry-run <old-path> <new-path>

Both paths must be relative to the project root.
"""

import os
import re
import sys
import subprocess
from pathlib import Path


def find_project_root() -> Path:
    """Find project root by locating the .git directory."""
    path = Path.cwd()
    while path != path.parent:
        if (path / ".git").exists():
            return path
        path = path.parent
    raise RuntimeError("Could not find project root (no .git directory found)")


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


def find_cpp_files(root: Path) -> list[Path]:
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
    dry_run: bool = False,
) -> list[tuple[str, str]]:
    """
    Update all #include "..." lines in file_path that resolve to old_abs.
    Rewrites them to point to new_abs, preserving the original include style.
    Returns a list of (old_include, new_include) pairs for each changed line.
    """
    try:
        content = file_path.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError):
        return []

    old_resolved = old_abs.resolve()
    lines = content.split("\n")
    changes: list[tuple[str, str]] = []
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
                changes.append((include_str, new_include))
                continue
        new_lines.append(line)

    if changes and not dry_run:
        try:
            file_path.write_text("\n".join(new_lines), encoding="utf-8")
        except OSError as e:
            print(f"  ⚠ Could not write {file_path}: {e}", file=sys.stderr)
            return []
    return changes


def update_own_includes(
    moved_file: Path,
    old_abs: Path,
    new_abs: Path,
    project_root: Path,
    dry_run: bool = False,
) -> list[tuple[str, str]]:
    """
    Update relative includes *within* the moved file itself.

    When a file moves to a new directory, its relative includes to other files
    must be recomputed from the new location. Project-root-relative includes
    ("magda/daw/...") are unchanged since they don't depend on file location.
    Returns a list of (old_include, new_include) pairs for each changed line.
    """
    try:
        content = moved_file.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError):
        return []

    lines = content.split("\n")
    changes: list[tuple[str, str]] = []
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
                changes.append((include_str, new_include))
                continue
        new_lines.append(line)

    if changes and not dry_run:
        try:
            moved_file.write_text("\n".join(new_lines), encoding="utf-8")
        except OSError as e:
            print(f"  ⚠ Could not write {moved_file}: {e}", file=sys.stderr)
            return []
    return changes


def update_cmake(cmake_path: Path, old_rel: str, new_rel: str, dry_run: bool = False) -> bool:
    """
    Update source entries in CMakeLists.txt.
    old_rel / new_rel are paths relative to the CMakeLists.txt directory.
    Only replaces lines that are source file entries (whole-line match, not comments).
    Returns True if a replacement was made.
    """
    try:
        content = cmake_path.read_text(encoding="utf-8")
    except OSError:
        return False

    new_content = re.sub(
        r"(?m)^(\s*)" + re.escape(old_rel) + r"(\s*)$",
        lambda m: m.group(1) + new_rel + m.group(2),
        content,
    )
    if new_content != content:
        if not dry_run:
            try:
                cmake_path.write_text(new_content, encoding="utf-8")
            except OSError as e:
                print(f"  ⚠ Could not write {cmake_path}: {e}", file=sys.stderr)
                return False
        return True
    return False


def main() -> None:
    args = sys.argv[1:]
    dry_run = "--dry-run" in args
    args = [a for a in args if a != "--dry-run"]

    if len(args) != 2:
        print("Usage: move-file.py [--dry-run] <old-path> <new-path>", file=sys.stderr)
        print("  Paths are relative to the project root.", file=sys.stderr)
        sys.exit(1)

    project_root = find_project_root()
    old_path = Path(args[0])
    new_path = Path(args[1])
    old_abs = (project_root / old_path).resolve()
    new_abs = (project_root / new_path).resolve()

    if not old_abs.exists():
        print(f"Error: '{old_path}' does not exist.", file=sys.stderr)
        sys.exit(1)
    if new_abs.exists():
        print(f"Error: '{new_path}' already exists.", file=sys.stderr)
        sys.exit(1)

    if dry_run:
        print(f"[dry-run] Would move {old_path} → {new_path}")
    else:
        print(f"Moving {old_path} → {new_path}")

    # Step 1: git mv (skipped in dry-run)
    if not dry_run:
        git_mv(old_abs, new_abs)
        print("  ✓ git mv completed")

    # In dry-run mode, new_abs doesn't exist yet; use old_abs as a stand-in for
    # scanning the moved file's own includes (path computation only needs the path).
    own_includes_file = new_abs if not dry_run else old_abs

    # Collect all C++ files
    cpp_files = find_cpp_files(project_root)

    # Step 2: Update includes in all other files that reference the old path
    updated_files: list[tuple[str, list[tuple[str, str]]]] = []
    for file_path in cpp_files:
        if file_path.resolve() == own_includes_file.resolve():
            continue
        changes = update_includes_in_file(file_path, old_abs, new_abs, project_root, dry_run=dry_run)
        if changes:
            updated_files.append((str(file_path.relative_to(project_root)), changes))

    verb = "Would update" if dry_run else "Updated"
    if updated_files:
        print(f"  ✓ {verb} includes in {len(updated_files)} file(s):")
        for f, changes in sorted(updated_files):
            print(f"    - {f}")
            if dry_run:
                for old_inc, new_inc in changes:
                    print(f'        "{old_inc}" → "{new_inc}"')
    else:
        print("  ✓ No external files needed include updates")

    # Step 3: Update relative includes within the moved file itself
    own_changes = update_own_includes(own_includes_file, old_abs, new_abs, project_root, dry_run=dry_run)
    if own_changes:
        verb2 = "Would update" if dry_run else "Updated"
        print(f"  ✓ {verb2} relative includes within {new_path}")
        if dry_run:
            for old_inc, new_inc in own_changes:
                print(f'        "{old_inc}" → "{new_inc}"')
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
                if update_cmake(cmake_path, old_cmake_rel, new_cmake_rel, dry_run=dry_run):
                    verb3 = "Would update" if dry_run else "Updated"
                    print(f"  ✓ {verb3} CMakeLists.txt: {old_cmake_rel} → {new_cmake_rel}")
                else:
                    print(
                        f"  ⚠ '{old_cmake_rel}' not found in CMakeLists.txt (may need manual update)"
                    )
            except ValueError:
                print("  ⚠ File is outside magda/daw/; CMakeLists.txt not updated")

    if dry_run:
        print("\n[dry-run] No files were modified. Re-run without --dry-run to apply.")
    else:
        print("\nDone! Review changes with: git diff")


if __name__ == "__main__":
    main()
