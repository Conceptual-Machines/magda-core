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
            mapping[output] = os.path.relpath(source, root)
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

    names = {os.path.basename(path) for path in wanted}
    found = {}
    current = None
    stale = False
    for line in proc.stdout.splitlines():
        if not line:
            continue
        if line[0] != " ":
            # "target: #deps N, deps mtime M (VALID|STALE)"
            current = line.split(":", 1)[0]
            stale = line.endswith("(STALE)")
            continue
        if current is None or stale:
            continue
        dep = line.strip()
        if dep[dep.rfind("/") + 1:] not in names:
            continue
        if dep[0] != "/":
            dep = os.path.join(build_dir, dep)
        dep = os.path.normpath(dep)
        if dep in wanted:
            found.setdefault(wanted[dep], set()).add(current)
    return found


def main(argv):
    build_dir = os.environ.get("BUILD_DIR", "cmake-build-debug")
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

    tus = set()
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
        tus |= eligible

    if unanalysable:
        # No translation unit compiles these, so nothing that could be found in
        # them reaches a binary. Worth saying, not worth blocking a push over.
        for header in unanalysable:
            print(f"note: nothing compiles {header}; not analysed", file=sys.stderr)

    for tu in sorted(tus):
        print(tu)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
