#!/usr/bin/env python3
"""
Reports comment-to-code line ratio for C++ sources under magda/.

Usage:
  scripts/comment_ratio.py                  # overall ratio + worst offenders
  scripts/comment_ratio.py --top 20         # show more offenders
  scripts/comment_ratio.py --check 60       # exit 1 if any file with >60 code
                                             # lines exceeds 60% comment ratio
  scripts/comment_ratio.py path/to/file.hpp # ratio for a single file
"""

import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE_EXTENSIONS = {".h", ".hpp", ".cpp", ".mm"}


def classify_lines(text: str) -> tuple[int, int]:
    """Returns (comment_lines, code_lines). Blank lines count as neither."""
    comment_lines = 0
    code_lines = 0
    in_block_comment = False

    for raw_line in text.split("\n"):
        line = raw_line.strip()
        if not line:
            continue

        if in_block_comment:
            comment_lines += 1
            if "*/" in line:
                in_block_comment = False
                rest = line.split("*/", 1)[1].strip()
                if rest and not rest.startswith("//"):
                    code_lines += 1
            continue

        if line.startswith("//"):
            comment_lines += 1
        elif line.startswith("/*"):
            comment_lines += 1
            if "*/" not in line:
                in_block_comment = True
        else:
            code_lines += 1

    return comment_lines, code_lines


def iter_source_files(root: Path):
    for path in root.rglob("*"):
        if path.suffix not in SOURCE_EXTENSIONS:
            continue
        if "third_party" in path.parts or "cmake-build" in " ".join(path.parts):
            continue
        yield path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("paths", nargs="*", help="specific files to report on")
    parser.add_argument("--top", type=int, default=10, help="how many worst offenders to list")
    parser.add_argument("--min-code", type=int, default=60, help="ignore files with fewer code lines than this")
    parser.add_argument("--check", type=int, metavar="PERCENT",
                         help="exit 1 if any qualifying file exceeds this comment/code percent")
    args = parser.parse_args()

    if args.paths:
        targets = [Path(p) for p in args.paths]
    else:
        targets = list(iter_source_files(ROOT / "magda"))

    results = []
    total_comment = 0
    total_code = 0
    min_code = 0 if args.paths else args.min_code
    for path in targets:
        comment, code = classify_lines(path.read_text(encoding="utf-8", errors="replace"))
        total_comment += comment
        total_code += code
        if code >= min_code:
            ratio = (comment / code * 100) if code else 0.0
            results.append((ratio, comment, code, path))

    results.sort(key=lambda r: r[0], reverse=True)

    if args.paths:
        for ratio, comment, code, path in results:
            print(f"{ratio:6.1f}%  {comment:6d} comment / {code:6d} code  {path}")
        return 0

    overall = (total_comment / total_code * 100) if total_code else 0.0
    print(f"Overall: {total_comment} comment lines / {total_code} code lines = {overall:.1f}%\n")

    print(f"Worst offenders (files with >= {args.min_code} code lines):")
    for ratio, comment, code, path in results[: args.top]:
        rel = path.relative_to(ROOT)
        print(f"{ratio:6.1f}%  {comment:6d} comment / {code:6d} code  {rel}")

    if args.check is not None:
        offenders = [r for r in results if r[0] > args.check]
        if offenders:
            print(f"\nFAIL: {len(offenders)} file(s) exceed {args.check}% comment ratio")
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
