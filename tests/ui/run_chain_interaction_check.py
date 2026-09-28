#!/usr/bin/env python3
"""Build/run the real chain UI regression check using a configured macOS Ninja app build.

Example: python3 tests/ui/run_chain_interaction_check.py --build-dir cmake-build-debug
Builds the app's libraries and UI objects as test dependencies, but does not link
or launch MAGDA. Keeping those dependencies current is important: NodeComponent
is a base class, so testing new derived components against stale objects is unsafe.
"""
import argparse
import json
import shlex
import subprocess
import tempfile
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=Path("cmake-build-debug"))
    parser.add_argument("--jobs", type=int, default=4)
    options = parser.parse_args()
    build = options.build_dir.resolve()
    source = Path(__file__).with_name("chain_interaction_check.cpp").resolve()

    def link_command():
        commands = subprocess.check_output(
            ["ninja", "-t", "commands", "magda_daw_app"], cwd=build, text=True
        ).splitlines()
        line = next(line for line in reversed(commands)
                    if "magda_daw_main.cpp.o" in line and " -o " in line and " -c " not in line)
        args = shlex.split(line)
        start = next(i for i, value in enumerate(args)
                     if Path(value).name in ("c++", "clang++", "g++"))
        end = args.index("&&", start) if "&&" in args[start:] else len(args)
        return args[start:end]

    link = link_command()
    targets = {line.rsplit(": ", 1)[0] for line in subprocess.check_output(
        ["ninja", "-t", "targets", "all"], cwd=build, text=True).splitlines()}
    dependencies = list(dict.fromkeys(value for value in link
                                     if value in targets and
                                     value.endswith((".o", ".a", ".so", ".dylib")) and
                                     not value.endswith("/magda_daw_main.cpp.o")))
    if not dependencies:
        raise RuntimeError("Could not find the app object/library targets; refusing a stale test build")
    subprocess.run(["cmake", "--build", str(build), "--parallel", str(options.jobs),
                    "--target", *dependencies], check=True)

    with tempfile.TemporaryDirectory(prefix="magda-chain-interaction-") as temporary:
        temporary = Path(temporary)
        commands = json.loads((build / "compile_commands.json").read_text())
        main_command = next(c for c in commands if c["file"].endswith("/magda_daw_main.cpp"))
        compile_args = shlex.split(main_command["command"])
        if Path(compile_args[0]).name == "ccache":
            compile_args.pop(0)
        obj = temporary / "chain_interaction_check.o"
        compile_args[compile_args.index("-o") + 1] = str(obj)
        compile_args[compile_args.index("-c") + 1] = str(source)
        subprocess.run(compile_args, cwd=main_command["directory"], check=True)
        link = link_command()
        executable = temporary / "chain_interaction_check"
        link[link.index("-o") + 1] = str(executable)
        link = [str(obj) if value.endswith("/magda_daw_main.cpp.o") else value for value in link]
        # App-only functions depend on its entry point; the check doesn't use them.
        link.append("-Wl,-dead_strip")
        subprocess.run(link, cwd=build, check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
