---
name: move-file
description: Move a C++ source file to a new location, updating all #include references and CMakeLists.txt entries automatically. Use when the user wants to move or rename a C++ file.
---

# Move C++ File

Automates moving a C++ source file and updating all related references.

## Usage

Run from the project root:

```bash
python3 .claude/skills/move-file/move-file.py [--dry-run] <old-path> <new-path>
```

Both paths are relative to the project root. Use `--dry-run` to preview what would change without modifying any files.

## Example

```bash
# Preview changes first (no files modified)
python3 .claude/skills/move-file/move-file.py --dry-run \
    magda/daw/core/Config.hpp \
    magda/daw/engine/Config.hpp

# Move a header from core/ to engine/
python3 .claude/skills/move-file/move-file.py \
    magda/daw/core/Config.hpp \
    magda/daw/engine/Config.hpp
```

## What It Does

1. **`git mv`** the file (preserves git history)
2. **Updates external `#include` references** in all `.cpp`/`.hpp` files:
   - Relative includes (`"../core/Config.hpp"`) → recomputed relative path from referencing file
   - Project-root-relative includes (`"magda/daw/core/Config.hpp"`) → updated path
3. **Updates includes within the moved file** (recomputes relative paths from the new location)
4. **Updates `magda/daw/CMakeLists.txt`** source entries (for `.cpp` files only)

## After Running

Review all changes before building:

```bash
git diff
make debug
```
