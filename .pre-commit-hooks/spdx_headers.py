#!/usr/bin/env python3
"""
Keeps the SPDX copyright and license header at the top of first-party C/C++ sources.

Usage:
  .pre-commit-hooks/spdx_headers.py [files...]          # check; exit 1 on a missing header
  .pre-commit-hooks/spdx_headers.py --fix [files...]    # prepend the header where missing

With no files, every tracked source under magda/, tests/ and tools/ is checked.
"""

import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HEADER = (
    "// SPDX-FileCopyrightText: Conceptual Machines\n"
    "// SPDX-License-Identifier: GPL-3.0-or-later\n"
)
SOURCE_EXTENSIONS = {".c", ".h", ".hpp", ".cpp", ".mm"}
FIRST_PARTY_DIRS = ("magda/", "tests/", "tools/")


def is_first_party_source(rel: str) -> bool:
    return rel.startswith(FIRST_PARTY_DIRS) and Path(rel).suffix in SOURCE_EXTENSIONS


def tracked_sources() -> list[str]:
    out = subprocess.run(["git", "ls-files", *FIRST_PARTY_DIRS], cwd=ROOT, check=True,
                         capture_output=True, text=True).stdout
    return [rel for rel in out.splitlines() if is_first_party_source(rel)]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--fix", action="store_true", help="prepend the header where it is missing")
    parser.add_argument("files", nargs="*")
    args = parser.parse_args()

    files = [f for f in args.files if is_first_party_source(f)] if args.files else tracked_sources()
    missing = []
    for rel in files:
        path = ROOT / rel
        text = path.read_text(encoding="utf-8")
        if text.startswith(HEADER):
            continue
        missing.append(rel)
        if args.fix:
            path.write_text(HEADER + "\n" + text, encoding="utf-8")

    if missing:
        verb = "Added the SPDX header to" if args.fix else "Missing the SPDX header:"
        print(f"{verb} {len(missing)} file(s)")
        for rel in missing[:50]:
            print(f"  {rel}")
        if not args.fix:
            print("Run: .pre-commit-hooks/spdx_headers.py --fix")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
