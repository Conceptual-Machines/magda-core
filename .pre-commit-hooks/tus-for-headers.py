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


def leaf(path):
    """Last component, whichever separator ninja wrote.

    A string slice rather than Path(path).name: this runs on every one of a few
    million dependency lines, and building a Path for each is far slower than
    rejecting on a substring.
    """
    return path[max(path.rfind("/"), path.rfind("\\")) + 1:]


def output_to_source(build_dir):
    """Ninja target name -> repo-relative source, from the compile database."""
    db = json.loads((build_dir / "compile_commands.json").read_text())
    mapping = {}
    for entry in db:
        if not entry.get("output"):
            continue
        # No resolve(): the database already records absolute paths, and a
        # syscall per entry costs more than the whole ninja dump.
        source = Path(entry["file"])
        if source.is_relative_to(ROOT):
            # Posix throughout: matched against "magda/" and handed to
            # clang-tidy, which takes forward slashes on Windows too.
            mapping[entry["output"]] = source.relative_to(ROOT).as_posix()
    return mapping


def dependents(build_dir, wanted):
    """Ninja outputs whose recorded dependencies include any of `wanted`."""
    proc = subprocess.run(["ninja", "-C", str(build_dir), "-t", "deps"],
                          capture_output=True, text=True, check=False)
    if proc.returncode != 0 or not proc.stdout:
        return None

    names = {leaf(str(path)) for path in wanted}
    found, current, stale = {}, None, False
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


def select(per_header, max_tus):
    """Pick TUs under a budget without starving any one header.

    Flattening every header's candidates and truncating let one broadly included
    header spend the whole budget, leaving another changed header with nothing
    that compiles it. So each header gets a representative first, even if that
    alone exceeds the budget - analysing every changed header matters more than
    the ceiling - and only the fan-out beyond that is rationed, round robin.
    """
    chosen, seen = [], set()
    for tus in per_header.values():
        for tu in tus:
            if tu not in seen:
                seen.add(tu)
                chosen.append(tu)
                break

    total = len({tu for tus in per_header.values() for tu in tus})

    remaining = [list(tus) for tus in per_header.values()]
    while remaining and (max_tus <= 0 or len(chosen) < max_tus):
        for tus in list(remaining):
            while tus:
                tu = tus.pop(0)
                if tu not in seen:
                    seen.add(tu)
                    chosen.append(tu)
                    break
            if not tus:
                remaining.remove(tus)
            if max_tus > 0 and len(chosen) >= max_tus:
                break
    return chosen, total


def in_scope(source):
    """Shipping code, matching .clang-tidy's own scope."""
    return source.startswith("magda/") and not source.startswith("magda/engine/")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="*", metavar="FILE")
    parser.add_argument("--missing", action="store_true")
    parser.add_argument("--build-dir", type=Path, default=Path("cmake-build-debug"))
    parser.add_argument("--max-tus", type=int, default=8)
    args = parser.parse_args()

    if not args.paths:
        return 0

    if args.missing:
        # Resolved by the code that reads the database, not by a grep in the
        # hook: it records absolute native paths, so matching a relative one by
        # string has to assume a separator.
        known = set(output_to_source(args.build_dir).values())
        for path in args.paths:
            if Path(path).as_posix() not in known:
                print(path)
        return 0

    wanted = {Path(h).resolve(): h for h in args.paths}
    found = dependents(args.build_dir, wanted)
    if found is None:
        print("could not read ninja dependency data", file=sys.stderr)
        return 1

    mapping = output_to_source(args.build_dir)

    per_header, unanalysable = {}, []
    for header in args.paths:
        sources = {mapping.get(o) for o in found.get(header, ())} - {None}
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

    for header in unanalysable:
        # Nothing compiles it, so nothing found in it reaches a binary. Worth
        # saying, not worth blocking a push over.
        print(f"note: nothing compiles {header}; not analysed", file=sys.stderr)

    chosen, total = select(per_header, args.max_tus)
    if total > len(chosen):
        print(f"note: changed headers reach {total} translation units, "
              f"analysing {len(chosen)}. Set CLANG_TIDY_MAX_TUS=0 for all.",
              file=sys.stderr)

    print("\n".join(chosen))
    return 0


if __name__ == "__main__":
    sys.exit(main())
