#!/usr/bin/env python3
"""Regenerate PlusCal translations without fighting the whitespace hook."""

import argparse
from pathlib import Path
import re
import subprocess
import tempfile
import zlib


def regenerate(source: Path, jar: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="magda-pluscal-") as work:
        generated = Path(work) / source.name
        generated.write_text(source.read_text())
        subprocess.run(
            ["java", "-cp", str(jar), "pcal.trans", "-nocfg", str(generated)],
            check=True,
        )
        lines = generated.read_text().splitlines()

    begin = next(i for i, line in enumerate(lines) if line.startswith(r"\* BEGIN TRANSLATION"))
    end = next(i for i, line in enumerate(lines) if line.startswith(r"\* END TRANSLATION"))
    checksum_pattern = r'chksum\(tla\) = "([0-9a-f]+)"'
    checksum = re.search(checksum_pattern, lines[begin])

    def translation_checksum() -> str:
        # pcal.Validator.checksum(Vector<String>) hashes concatenated lines.
        return format(zlib.crc32("".join(lines[begin + 1 : end]).encode()), "x")

    if checksum is None or checksum.group(1) != translation_checksum():
        raise RuntimeError(f"Unexpected translator checksum format in {source}")

    # The repository's trailing-whitespace hook otherwise invalidates TLC's
    # checksum. Only normalize freshly generated code, never bless a hand edit.
    lines = [line.rstrip() for line in lines]
    lines[begin] = re.sub(
        checksum_pattern, f'chksum(tla) = "{translation_checksum()}"', lines[begin]
    )
    source.write_text("\n".join(lines) + "\n")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--jar", type=Path, required=True, help="Pinned jar cached by scripts/tla.sh")
    parser.add_argument("specs", type=Path, nargs="+")
    args = parser.parse_args()
    for source in args.specs:
        regenerate(source, args.jar.resolve())


if __name__ == "__main__":
    main()
