#!/usr/bin/env python3
"""Map changed headers to the translation units that compile them.

`ninja -t deps` holds what the compiler itself recorded, so it covers transitive
includes. Grepping for direct includes does not: ChordTypes.hpp reaches many TUs
without a single .cpp naming it.

Prints one repo-relative TU path per line, and exits non-zero only when the
dependency data cannot be read at all. A header no translation unit compiles
produces a note instead: nothing found in it could reach a binary.
"""
import json
import os
import subprocess
import sys


def basename(path):
    """Last component, whichever separator ninja used.

    os.path.basename only knows the host's separator, and ninja on Windows
    writes backslashes, so a dependency would be compared whole against a bare
    file name, match nothing, and take the "nothing compiles this" path.
    """
    return path[max(path.rfind("/"), path.rfind("\\")) + 1:]


def load_output_to_source(build_dir):
    """Ninja target name -> repo-relative source, via the compile database."""
    with open(os.path.join(build_dir, "compile_commands.json")) as handle:
        db = json.load(handle)
    root = os.path.normpath(os.getcwd())
    mapping = {}
    for entry in db:
        output = entry.get("output")
        if not output:
            continue
        source = os.path.normpath(os.path.abspath(entry["file"]))
        if source.startswith(root + os.sep):
            # Forward slashes throughout: these are matched against "magda/" and
            # handed to clang-tidy, which takes them on Windows too.
            mapping[output] = os.path.relpath(source, root).replace(os.sep, "/")
    return mapping


def dependents(build_dir, wanted):
    """Ninja outputs whose recorded dependencies include any of `wanted`.

    The dump runs to millions of lines, so the hot loop does no syscalls: reject
    on basename first, and only then normalise a path. Resolving every line with
    realpath() instead took 150s against ninja's own 10.
    """
    proc = subprocess.run(
        ["ninja", "-C", build_dir, "-t", "deps"],
        capture_output=True, text=True, check=False,
    )
    if proc.returncode != 0 or not proc.stdout:
        return None

    names = {basename(path) for path in wanted}
    found = {}
    current = None
    stale = False
    for line in proc.stdout.splitlines():
        if not line:
            continue
        if line[0] != " ":
            # "target: #deps N, deps mtime M (VALID|STALE)". Split on the marker,
            # not the first colon, which on Windows is the drive letter's.
            marker = line.find(": #deps ")
            if marker == -1:
                current = None
                continue
            current = line[:marker]
            stale = line.endswith("(STALE)")
            continue
        if current is None or stale:
            continue
        dep = line.strip()
        if basename(dep) not in names:
            continue
        if not os.path.isabs(dep):
            dep = os.path.join(build_dir, dep)
        dep = os.path.normpath(dep)
        if dep in wanted:
            found.setdefault(wanted[dep], set()).add(current)
    return found


def select(per_header, max_tus):
    """Pick TUs under a budget without starving any one header.

    Flattening every header's candidates and truncating let one broadly
    included header spend the whole budget, leaving another changed header with
    nothing that compiles it. So each header gets a representative first, even
    if that alone exceeds the budget - analysing every changed header matters
    more than the ceiling - and only the fan-out beyond that is rationed, round
    robin so no single header takes it all.
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


def main(argv):
    build_dir = os.environ.get("BUILD_DIR", "cmake-build-debug")
    max_tus = int(os.environ.get("CLANG_TIDY_MAX_TUS", "8"))
    headers = argv[1:]
    if not headers:
        return 0

    wanted = {os.path.normpath(os.path.abspath(h)): h for h in headers}

    found = dependents(build_dir, wanted)
    if found is None:
        print("could not read ninja dependency data", file=sys.stderr)
        return 1

    mapping = load_output_to_source(build_dir)

    def in_scope(source):
        # Shipping code, matching .clang-tidy's own scope.
        return source.startswith("magda/") and not source.startswith("magda/engine/")

    per_header = {}
    unanalysable = []
    for header in headers:
        sources = {mapping.get(o) for o in found.get(header, ())}
        sources.discard(None)

        eligible = {s for s in sources if in_scope(s)}
        if not eligible:
            # Scoping has to be judged per header, after mapping. Deciding it
            # from the raw dependency set counted a header reached only through
            # a test TU as mapped, then filtered every candidate away and
            # returned success having chosen nothing.
            #
            # A header only tests compile is still analysed, through the test
            # TU: HeaderFilterRegex decides which headers get diagnostics, so
            # the header's own findings are reported either way.
            eligible = sources

        if not eligible:
            unanalysable.append(header)
            continue
        per_header[header] = sorted(eligible)

    if unanalysable:
        # No translation unit compiles these, so nothing that could be found in
        # them reaches a binary. Worth saying, not worth blocking a push over.
        for header in unanalysable:
            print(f"note: nothing compiles {header}; not analysed", file=sys.stderr)

    chosen, total = select(per_header, max_tus)
    if total > len(chosen):
        print(f"note: changed headers reach {total} translation units, "
              f"analysing {len(chosen)}. Set CLANG_TIDY_MAX_TUS=0 for all.",
              file=sys.stderr)

    for tu in chosen:
        print(tu)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
