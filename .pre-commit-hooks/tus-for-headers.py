#!/usr/bin/env python3
"""Map changed headers to the translation units that compile them.

`ninja -t deps` holds what the compiler itself recorded, so it covers transitive
includes. Grepping for direct includes does not: ChordTypes.hpp reaches many TUs
without a single .cpp naming it.

Prints one repo-relative TU path per line, and exits non-zero only when the
dependency data cannot be read at all. A header no translation unit compiles
produces a note instead: nothing found in it could reach a binary.

`--missing` answers a different question with the same path handling: which of
these files does no compile command mention.
"""
import argparse
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path.cwd()


def leaf(path: str) -> str:
    """Last component, whichever separator ninja wrote.

    A string slice rather than Path(path).name: this runs on every one of a few
    million dependency lines, and building a Path for each is far slower than
    rejecting on a substring.
    """
    return path[max(path.rfind("/"), path.rfind("\\")) + 1:]


def output_to_source(build_dir: Path) -> dict[str, Path]:
    """Ninja target name -> repo-relative source, from the compile database."""
    db = json.loads((build_dir / "compile_commands.json").read_text())
    mapping: dict[str, Path] = {}
    for entry in db:
        if not entry.get("output"):
            continue
        # No resolve(): the database already records absolute paths, and a
        # syscall per entry costs more than the whole ninja dump.
        source = Path(entry["file"])
        if source.is_relative_to(ROOT):
            mapping[entry["output"]] = source.relative_to(ROOT)
    return mapping


def dependents(build_dir: Path, wanted: dict[Path, str]) -> dict[str, set[str]] | None:
    """Ninja outputs whose recorded dependencies include any of `wanted`."""
    proc = subprocess.run(["ninja", "-C", str(build_dir), "-t", "deps"],
                          capture_output=True, text=True, check=False)
    if proc.returncode != 0 or not proc.stdout:
        return None

    names = {leaf(str(path)) for path in wanted}
    found: dict[str, set[str]] = {}
    current: str | None = None
    stale = False
    for line in proc.stdout.splitlines():
        if not line:
            continue
        if line[0] != " ":
            # "target: #deps N, deps mtime M (VALID|STALE)". Split on the
            # marker, not the first colon, which on Windows is the drive's.
            marker = line.find(": #deps ")
            current = None if marker == -1 else line[:marker]
            stale = line.endswith("(STALE)")
            continue
        if current is None or stale:
            continue
        dep = line.strip()
        if leaf(dep) not in names:
            continue
        resolved = Path(dep)
        if not resolved.is_absolute():
            resolved = build_dir / resolved
        # Only reached by the handful of lines the name filter let through, so
        # resolve()'s syscalls do not matter here.
        resolved = resolved.resolve()
        if resolved in wanted:
            found.setdefault(wanted[resolved], set()).add(current)
    return found


def one_per_header(per_header: dict[str, list[Path]]) -> list[Path]:
    """One TU for each header, distinct where the candidates allow it."""
    chosen: list[Path] = []
    seen: set[Path] = set()
    for tus in per_header.values():
        for tu in tus:
            if tu not in seen:
                seen.add(tu)
                chosen.append(tu)
                break
    return chosen


def fan_out(per_header: dict[str, list[Path]], already: list[Path],
            budget: int) -> list[Path]:
    """Further TUs, round robin so no one header spends the whole budget.

    A negative budget means no ceiling.
    """
    seen = set(already)
    extra: list[Path] = []
    remaining = [[tu for tu in tus if tu not in seen] for tus in per_header.values()]
    remaining = [tus for tus in remaining if tus]

    while remaining and (budget < 0 or len(extra) < budget):
        for tus in list(remaining):
            while tus:
                tu = tus.pop(0)
                if tu not in seen:
                    seen.add(tu)
                    extra.append(tu)
                    break
            if not tus:
                remaining.remove(tus)
            if 0 <= budget <= len(extra):
                break
    return extra


def select(per_header: dict[str, list[Path]], max_tus: int) -> tuple[list[Path], int]:
    """Pick TUs under a budget without starving any one header.

    Flattening every header's candidates and truncating let one broadly included
    header spend the whole budget, leaving another changed header with nothing
    that compiles it. So the representatives are taken first and never capped -
    analysing every changed header matters more than the ceiling - and only the
    fan-out beyond them is rationed.
    """
    chosen = one_per_header(per_header)
    total = len({tu for tus in per_header.values() for tu in tus})
    budget = -1 if max_tus <= 0 else max(0, max_tus - len(chosen))
    return chosen + fan_out(per_header, chosen, budget), total


def in_scope(source: Path) -> bool:
    """Shipping code, matching .clang-tidy's own scope."""
    return (source.is_relative_to("magda")
            and not source.is_relative_to("magda/engine"))


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="*", metavar="FILE")
    parser.add_argument("--missing", action="store_true")
    parser.add_argument("--build-dir", type=Path, default=Path("cmake-build-debug"))
    parser.add_argument("--max-tus", type=int, default=8)
    return parser


def report_missing(build_dir: Path, paths: list[str]) -> int:
    """Print the given paths that no compile command mentions.

    Answered here rather than by a grep in the hook: the database records
    absolute native paths, so matching a relative one by string has to assume a
    separator, and on Windows guessing wrong marks every file missing.
    """
    known = set(output_to_source(build_dir).values())
    for path in paths:
        if Path(path) not in known:
            print(path)
    return 0


def group_by_header(
    headers: list[str],
    found: dict[str, set[str]],
    mapping: dict[str, Path],
) -> tuple[dict[str, list[Path]], list[str]]:
    """Candidate TUs per header, and the headers nothing compiles."""
    per_header: dict[str, list[Path]] = {}
    unanalysable: list[str] = []
    for header in headers:
        sources = {tu for o in found.get(header, ()) if (tu := mapping.get(o))}
        # Scoping has to be judged per header, after mapping. Deciding it from
        # the raw dependency set counted a header reached only through a test TU
        # as mapped, then filtered every candidate away and chose nothing.
        # Falling back to that test TU still analyses the header itself, since
        # HeaderFilterRegex decides which headers get diagnostics.
        eligible = {s for s in sources if in_scope(s)} or sources
        if eligible:
            per_header[header] = sorted(eligible)
        else:
            unanalysable.append(header)
    return per_header, unanalysable


def main() -> int:
    args = build_parser().parse_args()
    if not args.paths:
        return 0
    if args.missing:
        return report_missing(args.build_dir, args.paths)

    wanted = {Path(h).resolve(): h for h in args.paths}
    found = dependents(args.build_dir, wanted)
    if found is None:
        print("could not read ninja dependency data", file=sys.stderr)
        return 1

    per_header, unanalysable = group_by_header(
        args.paths, found, output_to_source(args.build_dir))

    for header in unanalysable:
        # Nothing compiles it, so nothing found in it reaches a binary. Worth
        # saying, not worth blocking a push over.
        print(f"note: nothing compiles {header}; not analysed", file=sys.stderr)

    chosen, total = select(per_header, args.max_tus)
    if total > len(chosen):
        print(f"note: changed headers reach {total} translation units, "
              f"analysing {len(chosen)}. Set CLANG_TIDY_MAX_TUS=0 for all.",
              file=sys.stderr)

    # Posix at the boundary: these go to clang-tidy and are matched against
    # "magda/" in the hook, and str(Path) would give backslashes on Windows.
    print("\n".join(tu.as_posix() for tu in chosen))
    return 0


if __name__ == "__main__":
    sys.exit(main())
