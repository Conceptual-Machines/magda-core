#!/usr/bin/env python3
"""Run clang-tidy over what a push touches and fail on findings.

One script rather than a shell driver around a Python resolver. The split cost
more than it saved: ninja names its outputs relative to the build directory and
the compile database records them absolute, and with the join spread across two
languages nothing noticed that the header half of the gate matched nothing at
all. It also meant a bash 4 dependency for `mapfile`, which macOS does not have
at /bin/bash and which no amount of installing fixes while a package manager's
prefix sits after /bin on PATH.

`ninja -t deps` holds what the compiler itself recorded, so a header maps to the
translation units that compile it through its transitive includes. Grepping for
direct includes does not: ChordTypes.hpp reaches many TUs without a single .cpp
naming it.

clang-tidy exits 0 for a plain warning, so --warnings-as-errors is what makes a
check binding. Exit code is the whole signal: clang-tidy also writes an
unconditional "N warnings generated." tally, so deciding pass/fail from output
is how pocc/pre-commit-hooks v1.4.0 gets this wrong.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from glob import glob
from pathlib import Path

ROOT = Path.cwd()
SHIPPING = Path("magda")
ENGINE = SHIPPING / "engine"

# The bugprone/cert half of .clang-tidy, minus five checks that do not exist on
# every clang-tidy this runs on.
#
# #2530 deleted those five having "verified" against LLVM 20.1.4, where
# `--checks=-*,bugprone-signed-bitwise` answers "no checks enabled" and the
# check is spelled hicpp-signed-bitwise instead. A newer clang-tidy has them
# under bugprone-, the CI runner has that newer one, and with WarningsAsErrors
# now binding the first push to dev failed on findings the local sweep could
# not see: `channel &= ~1` and `1 << 11` (signed-bitwise), a file-scope
# juce::Identifier whose constructor can throw (throwing-static-initialization)
# and `for (float db = ...; db -= 20.0f)` (float-loop-counter).
#
# A check name absent from one machine's --list-checks is not an absent check.
# An unknown name is silently ignored, so a sweep that finds nothing looks
# identical either way; the exclusions stay until the findings are triaged on
# the version that actually reports them.
CHECKS = ",".join([
    "-*",
    "bugprone-*",
    "cert-oop54-cpp",
    "-bugprone-easily-swappable-parameters",
    "-bugprone-narrowing-conversions",
    "-bugprone-chained-comparison",
    "-bugprone-suspicious-include",
    "-bugprone-exception-escape",
    "-bugprone-unchecked-optional-access",
    "-bugprone-swapped-arguments",
    "-bugprone-branch-clone",
    "-bugprone-empty-catch",
    "-bugprone-signed-char-misuse",
    "-bugprone-misplaced-widening-cast",
    "-bugprone-nondeterministic-pointer-iteration-order",
    "-bugprone-macro-parentheses",
    # Only on a clang-tidy new enough to carry them; see above.
    "-bugprone-signed-bitwise",
    "-bugprone-throwing-static-initialization",
    "-bugprone-derived-method-shadowing-base-method",
    "-bugprone-float-loop-counter",
    "-bugprone-unchecked-string-to-number-conversion",
])

# Sources in the tree but in no compile command, so an absent database entry is
# expected rather than a stale build. Anything else missing is an error.
UNBUILT_OK = {
    # Built only when MAGDA_PRO_DEVICES is OFF, where the real pack replaces them.
    Path("magda/daw/device_packs/pro_stub/ProDevicePack.cpp"),
    Path("magda/daw/device_packs/pro_stub/ProStubPlugin.cpp"),
    # In no CMake target and referenced only by each other.
    Path("magda/daw/ui/components/automation/AutomationPointComponent.cpp"),
    Path("magda/daw/ui/components/automation/BezierHandleComponent.cpp"),
    Path("magda/daw/ui/components/automation/TensionHandleComponent.cpp"),
}

# clang-tidy ships with LLVM and LLVM is commonly installed unlinked, so it is
# often not on PATH at all. Not one package manager's path: a glob over where
# the usual ones put it, after the two ways a user can say outright.
LLVM_GLOBS = (
    "/opt/homebrew/opt/llvm/bin/clang-tidy",
    "/usr/local/opt/llvm/bin/clang-tidy",
    "/usr/lib/llvm-*/bin/clang-tidy",
    "/opt/local/libexec/llvm-*/bin/clang-tidy",
)

TALLY = re.compile(r"^[\d,]+ warnings? generated\.$")


def leaf(path: str) -> str:
    """Last component, whichever separator ninja wrote.

    A slice rather than Path(path).name because this is the one hot line here:
    over the 2.64M dependency lines of a full dump, 1.1s against 7.6s, on a run
    that otherwise takes 12s and that somebody is waiting on to push.
    """
    return path[max(path.rfind("/"), path.rfind("\\")) + 1:]


def output_key(build_dir: Path, output: str) -> str:
    """One shape for a ninja output, whichever end it came from.

    `ninja -t deps` names its outputs relative to the build directory and the
    compile database records them absolute, so the two only join once both are
    put in the same form. as_posix() because ninja on Windows writes the
    separator the other way round.
    """
    path = Path(output)
    if path.is_absolute():
        try:
            return path.relative_to(build_dir).as_posix()
        except ValueError:
            return path.as_posix()
    return path.as_posix()


def output_to_source(build_dir: Path) -> dict[str, Path]:
    """Ninja target name -> repo-relative source, from the compile database."""
    db = json.loads((build_dir / "compile_commands.json").read_text())
    mapping: dict[str, Path] = {}
    resolved_build = build_dir.resolve()
    for entry in db:
        if not entry.get("output"):
            continue
        # No resolve(): the database already records absolute paths, and a
        # syscall per entry costs more than the whole ninja dump.
        source = Path(entry["file"])
        if source.is_relative_to(ROOT):
            mapping[output_key(resolved_build, entry["output"])] = source.relative_to(ROOT)
    return mapping


def dependents(build_dir: Path, wanted: dict[Path, str]) -> dict[str, set[str]] | None:
    """Ninja outputs whose recorded dependencies include any of `wanted`."""
    proc = subprocess.run(["ninja", "-C", str(build_dir), "-t", "deps"],
                          capture_output=True, text=True, check=False)
    if proc.returncode != 0 or not proc.stdout:
        return None

    names = {leaf(str(path)) for path in wanted}
    resolved_build = build_dir.resolve()
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
            current = None if marker == -1 else output_key(resolved_build, line[:marker])
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
    return source.is_relative_to(SHIPPING) and not source.is_relative_to(ENGINE)


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


def find_clang_tidy() -> str | None:
    """The binary to run, or None."""
    named = os.environ.get("CLANG_TIDY")
    if named:
        return named

    found = shutil.which("clang-tidy")
    if found:
        return found

    for pattern in LLVM_GLOBS:
        matches = sorted(glob(pattern))
        if matches:
            return matches[-1]
    return None


def clang_tidy_version(binary: str) -> str | None:
    """The major version of `binary`, or None if it will not say."""
    proc = subprocess.run([binary, "--version"], capture_output=True, text=True, check=False)
    if proc.returncode != 0:
        return None
    found = re.search(r"LLVM version (\d+)", proc.stdout)
    return found.group(1) if found else None


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="*", metavar="FILE")
    parser.add_argument("--build-dir", type=Path,
                        default=Path(os.environ.get("BUILD_DIR", "cmake-build-debug")))
    parser.add_argument("--max-tus", type=int,
                        default=int(os.environ.get("CLANG_TIDY_MAX_TUS", "8")))
    parser.add_argument("--jobs", type=int, default=min(8, (os.cpu_count() or 2)))
    parser.add_argument("--advisory", action="store_true",
                        default=os.environ.get("CLANG_TIDY_ADVISORY") == "1",
                        help="report findings without failing")
    return parser


def check_sources(sources: list[Path], known: set[Path]) -> tuple[list[Path], bool]:
    """The sources to analyse, and whether any of them should have been built."""
    targets: list[Path] = []
    failed = False
    for source in sources:
        if source in known:
            targets.append(source)
        elif source in UNBUILT_OK:
            print(f"note: {source} is in no compile command, which is expected for it.")
        else:
            print(f"{source} has no compile_commands.json entry.", file=sys.stderr)
            print("Run 'make debug'. If it belongs to no target, say so in "
                  "UNBUILT_OK in this script rather than leaving it unanalysed.",
                  file=sys.stderr)
            failed = True
    return targets, failed


def headers_to_tus(build_dir: Path, headers: list[Path], max_tus: int,
                   mapping: dict[str, Path]) -> tuple[list[Path], bool]:
    """The TUs that compile the changed headers, under the budget."""
    wanted = {header.resolve(): header.as_posix() for header in headers}
    found = dependents(build_dir, wanted)
    if found is None:
        # Either the dependency data is unreadable or ninja has recorded none.
        # Reporting clean off the back of either is the failure this gate exists
        # to prevent.
        print("Could not read ninja dependency data.", file=sys.stderr)
        print("Run 'make debug' so ninja has recorded its dependencies.", file=sys.stderr)
        return [], True

    per_header, unanalysable = group_by_header(
        [header.as_posix() for header in headers], found, mapping)

    for header in unanalysable:
        # Nothing compiles it, so nothing found in it reaches a binary. Worth
        # saying, not worth blocking a push over.
        print(f"note: nothing compiles {header}; not analysed", file=sys.stderr)

    chosen, total = select(per_header, max_tus)
    if total > len(chosen):
        print(f"note: changed headers reach {total} translation units, "
              f"analysing {len(chosen)}. Set CLANG_TIDY_MAX_TUS=0 for all.",
              file=sys.stderr)
    return chosen, False


def analyse(binary: str, build_dir: Path, target: Path) -> tuple[int, str]:
    """clang-tidy over one TU: its status, and what it had to say."""
    proc = subprocess.run(
        [binary, target.as_posix(), f"--checks={CHECKS}",
         f"--warnings-as-errors={CHECKS}", "--quiet", f"-p={build_dir}"],
        capture_output=True, text=True, check=False)

    # The tally counts compiler warnings in the TU, not findings, and reads as a
    # contradiction next to a passing gate. Dropping it is safe because pass or
    # fail is the exit code here, never the output.
    noise = "\n".join(line for line in proc.stderr.splitlines()
                       if not TALLY.match(line.strip()))
    return proc.returncode, (proc.stdout + noise).strip()


def main() -> int:
    args = build_parser().parse_args()
    advisory = args.advisory
    if not args.paths:
        return 0

    binary = find_clang_tidy()
    if binary is None:
        print("clang-tidy not found. Install LLVM, put it on PATH, or set "
              "CLANG_TIDY=/path/to/clang-tidy.", file=sys.stderr)
        return 1

    # Which binary, always. Two self-hosted macOS runners once carried different
    # LLVMs, and a check that exists on one and not the other is silent: an
    # unknown name is ignored, so the gate passes and dev goes red on the next
    # push that lands on the other machine.
    version = clang_tidy_version(binary)
    print(f"clang-tidy {version or 'unknown'} ({binary})", flush=True)

    required = os.environ.get("CLANG_TIDY_REQUIRE_MAJOR")
    if required and version != required:
        print(f"This gate is pinned to LLVM {required} and found "
              f"{version or 'a build that will not report its version'}.", file=sys.stderr)
        print(f"Findings differ between releases, so an unpinned gate reports a "
              f"different set depending on which runner takes the job. Install "
              f"LLVM {required} and point CLANG_TIDY at it, or change "
              f"CLANG_TIDY_REQUIRE_MAJOR if the pin is meant to move.",
              file=sys.stderr)
        # An advisory run does not block, and least of all on its own toolchain:
        # a runner without the pinned keg is not a finding about the code under
        # review. Homebrew moved `llvm` to 23 and the second macOS runner has no
        # llvm@20, which blocked every PR on a step that cannot fail on findings.
        # Binding runs still stop here, or the pin would mean nothing.
        if advisory:
            print("\nSKIPPED - wrong clang-tidy. ADVISORY ONLY - not failing.",
                  file=sys.stderr)
            return 0
        return 1

    database = args.build_dir / "compile_commands.json"
    if not database.is_file():
        print(f"{database} not found. Run 'make debug' first.", file=sys.stderr)
        return 1

    sources = [Path(p) for p in args.paths if Path(p).suffix == ".cpp"]
    headers = [Path(p) for p in args.paths if Path(p).suffix in {".h", ".hpp"}]

    mapping = output_to_source(args.build_dir)
    targets, failed = check_sources(sources, set(mapping.values()))

    if headers:
        # Kept apart so the budget can never drop a file the push changed: it
        # rations the header fan-out and nothing else.
        expanded, broke = headers_to_tus(args.build_dir, headers, args.max_tus, mapping)
        failed = failed or broke
        chosen = set(targets)
        targets += [tu for tu in expanded if tu not in chosen]

    if not targets:
        return 1 if failed else 0

    with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        results = list(pool.map(lambda tu: analyse(binary, args.build_dir, tu), targets))

    for status, said in results:
        if said:
            print(said, file=sys.stderr)
        failed = failed or status != 0

    print(f"clang-tidy: {len(targets)} translation unit(s) checked")

    if failed:
        if advisory:
            # Loud, and never silent about being advisory. The hook this replaced
            # ended its command with `|| true` and read as a passing gate for six
            # sweeps; an advisory run has to be obviously advisory or it becomes
            # the same lie.
            print("\nclang-tidy findings above. ADVISORY ONLY - not failing.", file=sys.stderr)
            print("Gating is off until magda/engine is written and swept; see "
                  "WarningsAsErrors in .clang-tidy.", file=sys.stderr)
            return 0
        print("\nclang-tidy gate failed; see above.", file=sys.stderr)
        print("For a finding, fix it or add a NOLINT with a reason if it is wrong.",
              file=sys.stderr)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
