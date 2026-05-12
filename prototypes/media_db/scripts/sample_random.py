"""Pick N random audio files from a source dir and symlink them into a target.

Used to build small reproducible test corpora from a large sample library.

Usage:
    python scripts/sample_random.py SRC_DIR TARGET_DIR --count 150 --seed 42
"""

from __future__ import annotations

import argparse
import random
from pathlib import Path

AUDIO_EXTS = {".wav", ".aif", ".aiff", ".mp3", ".flac", ".ogg", ".m4a"}


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("src", type=Path)
    p.add_argument("target", type=Path)
    p.add_argument("--count", type=int, default=150)
    p.add_argument("--seed", type=int, default=42)
    args = p.parse_args()

    files = [
        f for f in args.src.rglob("*")
        if f.is_file() and f.suffix.lower() in AUDIO_EXTS
    ]
    print(f"found {len(files)} audio files in {args.src}")

    rng = random.Random(args.seed)
    picked = rng.sample(files, min(args.count, len(files)))

    args.target.mkdir(parents=True, exist_ok=True)
    for old in args.target.iterdir():
        if old.is_symlink():
            old.unlink()

    for src in picked:
        # encode the full relative path into the symlink name so collisions
        # across packs don't clobber each other
        rel = src.relative_to(args.src)
        name = "__".join(rel.parts).replace(" ", "_")
        link = args.target / name
        link.symlink_to(src)

    print(f"symlinked {len(picked)} files into {args.target}")


if __name__ == "__main__":
    main()
