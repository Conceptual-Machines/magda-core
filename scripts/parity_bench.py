#!/usr/bin/env python3
"""
Parity envelope benchmark (#2082): the native engine measured against Tracktion on the
real-project corpus, at matched block sizes, on this machine.

Runs magda_parity_bench once per engine, project and block size, each in its own process,
appends the run to this machine's history, and fails when a native figure misses the
threshold the fork's figure from the same run sets (tests/parity/thresholds.json).

Usage:
  scripts/parity_bench.py                                  # every project at 128 and 512
  scripts/parity_bench.py --projects project.demo --block-sizes 256
  scripts/parity_bench.py --report                         # re-judge the last recorded run
"""

import argparse
import datetime
import hashlib
import json
import math
import os
import platform
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
THRESHOLDS = ROOT / "tests" / "parity" / "thresholds.json"
RESULT_PREFIX = "PARITY-RESULT "
ENGINES = ("tracktion", "native")
SCHEMA = 1


def find_bench(explicit, build_dir):
    if explicit:
        return Path(explicit)
    for candidate in sorted((ROOT / build_dir).glob("**/magda_parity_bench")):
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return candidate
    sys.exit("magda_parity_bench not found; build it with `make parity-bench-build`")


def bench_environment():
    """A sandbox of its own: the engines read and write settings under HOME."""
    home = ROOT / ".cache" / "parity-home"
    home.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ)
    env["HOME"] = str(home)
    env["CFFIXED_USER_HOME"] = str(home)
    return env


def run_bench(bench, args, timeout):
    """Runs the bench and returns the JSON object it printed, or a failure record."""
    try:
        completed = subprocess.run([str(bench)] + args, capture_output=True, text=True,
                                   timeout=timeout, env=bench_environment())
    except subprocess.TimeoutExpired:
        return {"status": "failed", "reason": "timed out after %d s" % timeout}

    for line in reversed(completed.stdout.splitlines()):
        if line.startswith(RESULT_PREFIX):
            return json.loads(line[len(RESULT_PREFIX):])

    tail = (completed.stderr or completed.stdout).strip().splitlines()[-5:]
    return {"status": "failed",
            "reason": "exit %d, no result: %s" % (completed.returncode, " | ".join(tail))}


# --- the machine ------------------------------------------------------------------------


def command_output(command):
    try:
        return subprocess.run(command, capture_output=True, text=True, timeout=10).stdout.strip()
    except (OSError, subprocess.TimeoutExpired):
        return ""


def cpu_brand():
    system = platform.system()
    if system == "Darwin":
        return command_output(["sysctl", "-n", "machdep.cpu.brand_string"])
    if system == "Linux":
        try:
            for line in Path("/proc/cpuinfo").read_text().splitlines():
                if line.startswith("model name"):
                    return line.split(":", 1)[1].strip()
        except OSError:
            pass
    return platform.processor()


def memory_bytes():
    system = platform.system()
    if system == "Darwin":
        value = command_output(["sysctl", "-n", "hw.memsize"])
        return int(value) if value.isdigit() else 0
    if system == "Linux":
        try:
            for line in Path("/proc/meminfo").read_text().splitlines():
                if line.startswith("MemTotal:"):
                    return int(line.split()[1]) * 1024
        except OSError:
            pass
    if system == "Windows":
        import ctypes

        class MemoryStatus(ctypes.Structure):
            _fields_ = [("dwLength", ctypes.c_ulong), ("dwMemoryLoad", ctypes.c_ulong),
                        ("ullTotalPhys", ctypes.c_ulonglong),
                        ("ullAvailPhys", ctypes.c_ulonglong),
                        ("ullTotalPageFile", ctypes.c_ulonglong),
                        ("ullAvailPageFile", ctypes.c_ulonglong),
                        ("ullTotalVirtual", ctypes.c_ulonglong),
                        ("ullAvailVirtual", ctypes.c_ulonglong),
                        ("ullAvailExtendedVirtual", ctypes.c_ulonglong)]

        status = MemoryStatus()
        status.dwLength = ctypes.sizeof(MemoryStatus)
        if ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(status)):
            return status.ullTotalPhys
    return 0


def describe_machine():
    """What the machine is, and the key its history is filed under.

    The key hashes the hardware only, so an OS update keeps the history it is compared with;
    the OS is recorded beside every run so a step in the numbers can be read against it.
    """
    machine = {
        "host": platform.node().split(".")[0],
        "system": platform.system(),
        "release": platform.release(),
        "arch": platform.machine(),
        "cpu": cpu_brand(),
        "cores": os.cpu_count() or 0,
        "memory_gb": round(memory_bytes() / (1024 ** 3), 1),
    }
    hardware = "%s|%s|%d|%s" % (machine["cpu"], machine["arch"], machine["cores"],
                                machine["memory_gb"])
    slug = re.sub(r"[^A-Za-z0-9]+", "-", machine["host"]).strip("-").lower() or "machine"
    machine["id"] = "%s-%s" % (slug, hashlib.sha1(hardware.encode()).hexdigest()[:8])
    return machine


def history_path(args, machine):
    root = Path(args.history_dir or os.environ.get("MAGDA_PARITY_HISTORY")
                or Path.home() / ".magda-parity")
    return root / (machine["id"] + ".jsonl")


def read_history(path):
    if not path.exists():
        return []
    runs = []
    for line in path.read_text().splitlines():
        if line.strip():
            runs.append(json.loads(line))
    return runs


def git_state():
    commit = command_output(["git", "-C", str(ROOT), "rev-parse", "HEAD"])
    dirty = bool(command_output(["git", "-C", str(ROOT), "status", "--porcelain", "-uno"]))
    return commit, dirty


# --- the figures ------------------------------------------------------------------------


def figures(result):
    """The gated figures of one measurement, flattened; absent when not measured."""
    if result.get("status") != "ok":
        return {}
    values = {
        "cpu_mean_us": result["cpu"]["mean_us"],
        "cpu_p99_us": result["cpu"]["p99_us"],
        "session_mb": result["memory"]["session_mb"],
        "load_ms": result["load_ms"],
    }
    latency = result.get("latency", {})
    if latency.get("status") == "ok":
        values["latency_samples"] = latency["reported_samples"]
    return values


def key_of(result):
    return (result["project"], result["block_size"])


def envelope_gap_db(fork, native):
    """Median distance between the two runs' output loudness, in dB, or None without it.

    Two engines timed on different audio are not a comparison: a transport that started in the
    wrong place once made native look fast on material the fork never had to render (#2786).
    """
    a, b = fork.get("loudness") or [], native.get("loudness") or []
    count = min(len(a), len(b))
    if count == 0:
        return None
    level = lambda peak: 20.0 * math.log10(max(peak, 1.0e-4))
    gaps = sorted(abs(level(a[i]) - level(b[i])) for i in range(count))
    return gaps[count // 2]


def judge(run, thresholds):
    """Every native figure against the fork's from the same run; returns (rows, failures)."""
    metrics = thresholds["metrics"]
    declared = {(d["project"], d["metric"]): d["mechanism"]
                for d in thresholds.get("divergences", [])}

    by_key = {}
    for result in run["results"]:
        by_key.setdefault(key_of(result), {})[result["engine"]] = result

    rows, failures = [], []
    for (project, block), engines in sorted(by_key.items()):
        fork, native = engines.get("tracktion"), engines.get("native")
        if fork is None or native is None:
            continue

        # A machine without a project's plugin runs neither engine on it; anything else that
        # stops a measurement stops the verdict with it.
        unmeasured = [e for e in (fork, native) if e.get("status") != "ok"]
        if unmeasured:
            for engine in unmeasured:
                if engine.get("status") != "not_run":
                    failures.append("%s @%d: %s failed: %s" % (
                        project, block, engine["engine"], engine.get("reason", "")))
            status = unmeasured[0].get("status")
            rows.append((project, block, "-", None, None, None,
                         "%s: %s" % (status.replace("_", " "), unmeasured[0].get("reason", ""))))
            continue

        gap = envelope_gap_db(fork, native)
        agreement = thresholds.get("render_agreement_db")
        if gap is not None and agreement is not None and gap > agreement:
            reason = "the engines rendered different audio, %.1f dB apart" % gap
            failures.append("%s @%d: %s, so nothing was compared" % (project, block, reason))
            rows.append((project, block, "-", None, None, None, reason))
            continue

        fork_values, native_values = figures(fork), figures(native)
        for metric, rule in metrics.items():
            if metric not in fork_values or metric not in native_values:
                continue
            limit = fork_values[metric] * rule["ratio"] + rule["slack"]
            value = native_values[metric]
            verdict = "ok"
            if value > limit:
                mechanism = declared.get((project, metric))
                if mechanism:
                    verdict = "declared"
                else:
                    verdict = "FAIL"
                    failures.append("%s @%d: native %s %.1f exceeds %.1f (fork %.1f)" % (
                        project, block, metric, value, limit, fork_values[metric]))
            rows.append((project, block, metric, fork_values[metric], value, limit, verdict))

        # A reported latency that disagrees with the measured one is a failure on its own.
        latency = native.get("latency", {})
        if latency.get("status") != "ok":
            failures.append("%s @%d: native latency unmeasured: %s" % (
                project, block, latency.get("reason", "")))
        elif latency["reported_samples"] != latency["measured_samples"]:
            failures.append("%s @%d: native reports %d samples of latency, measured %d" % (
                project, block, latency["reported_samples"], latency["measured_samples"]))

    return rows, failures


def previous_figures(history, run):
    """This machine's last earlier run with the same build, as {(engine, project, block): figures}."""
    for earlier in reversed(history):
        if earlier is run or earlier["settings"] != run["settings"]:
            continue
        return {(r["engine"],) + key_of(r): figures(r) for r in earlier["results"]}, earlier
    return {}, None


def fork_latency_notes(run):
    notes = []
    for result in run["results"]:
        latency = result.get("latency", {})
        if result["engine"] == "tracktion" and latency.get("status") == "ok" and \
                latency["reported_samples"] != latency["measured_samples"]:
            notes.append("%s @%d: the fork reports %d samples of latency, measured %d" % (
                result["project"], result["block_size"], latency["reported_samples"],
                latency["measured_samples"]))
    return notes


def print_report(run, history, thresholds):
    rows, failures = judge(run, thresholds)
    earlier_figures, earlier = previous_figures(history, run)

    print("\nmagda-parity-envelope  %s  %s  %s build  %s%s" % (
        run["machine"]["id"], run["time"], run["settings"]["build"], run["commit"][:10],
        " (dirty)" if run["dirty"] else ""))
    if earlier is not None:
        print("  change is against this machine's run of %s at %s" % (
            earlier["time"], earlier["commit"][:10]))

    header = "  %-22s %5s  %-16s %11s %11s %11s  %-9s %s" % (
        "project", "block", "metric", "tracktion", "native", "limit", "verdict", "native change")
    print(header)
    for project, block, metric, fork, native, limit, verdict in rows:
        if fork is None:
            print("  %-22s %5d  %-16s %11s %11s %11s  %s" % (project, block, metric, "", "", "",
                                                            verdict))
            continue
        change = ""
        before = earlier_figures.get(("native", project, block), {}).get(metric)
        if before:
            change = "%+.1f%%" % (100.0 * (native - before) / before)
        print("  %-22s %5d  %-16s %11.1f %11.1f %11.1f  %-9s %s" % (
            project, block, metric, fork, native, limit, verdict, change))

    for note in fork_latency_notes(run):
        print("  note: " + note)

    if failures:
        print("\n%d failure(s):" % len(failures))
        for failure in failures:
            print("  " + failure)
    else:
        print("\nevery native figure is inside the fork's envelope")
    return not failures


# --- the run ----------------------------------------------------------------------------


def list_projects(bench, timeout):
    listing = run_bench(bench, ["--list"], timeout)
    if listing.get("status") != "ok":
        sys.exit("magda_parity_bench --list failed: %s" % listing.get("reason", ""))
    return listing


def other_load():
    """What else is using this machine, as a note, or empty when it is quiet enough to measure."""
    if platform.system() == "Windows":
        return ""

    def sample():
        heavy = {}
        for line in command_output(["ps", "-Ao", "pcpu,pid,comm", "-r"]).splitlines()[1:9]:
            parts = line.strip().split(None, 2)
            if len(parts) == 3 and float(parts[0]) >= 25.0 and int(parts[1]) != os.getpid():
                heavy[int(parts[1])] = "%s at %s%%" % (parts[2].rsplit("/", 1)[-1], parts[0])
        return heavy

    # Twice, a second apart: a daemon's momentary burst is not a build.
    first = sample()
    time.sleep(1.0)
    second = sample()
    return ", ".join(second[pid] for pid in second if pid in first)


def median_run(runs):
    """The run in the middle by mean callback time, with every repeat's figure kept.

    A repeat that failed fails the cell: a crash in one run of three is a finding, not noise.
    """
    def summary(r):
        kept = {k: r.get(k) for k in ("status", "reason", "output_peak")}
        kept["cpu"] = r.get("cpu")
        return kept

    # A failure outranks a project this machine cannot run, which outranks a measurement.
    failed = [r for r in runs if r.get("status") not in ("ok", "not_run")]
    not_run = [r for r in runs if r.get("status") == "not_run"]
    if failed or not_run:
        chosen = dict((failed or not_run)[0])
        chosen["reason"] = "%d of %d repeats: %s" % (len(failed or not_run), len(runs),
                                                    chosen.get("reason", ""))
        chosen["repeats"] = [summary(r) for r in runs]
        return chosen
    good = sorted(runs, key=lambda r: r["cpu"]["mean_us"])
    chosen = dict(good[len(good) // 2])
    chosen["repeats"] = [summary(r) for r in runs]
    return chosen


def measure(args, bench):
    listing = list_projects(bench, args.timeout)
    projects = [p["name"] for p in listing["projects"]]
    if args.projects:
        unknown = sorted(set(args.projects) - set(projects))
        if unknown:
            sys.exit("not in the corpus: " + ", ".join(unknown))
        projects = [p for p in projects if p in args.projects]

    busy = other_load()
    if busy:
        print("machine is busy: " + busy)
        if not args.allow_busy:
            sys.exit("a figure taken beside that is noise; close it or pass --allow-busy")

    results = []
    for project in projects:
        for block in args.block_sizes:
            # The engines alternate, so a drift in the machine lands on both alike, and each
            # cell keeps the median of its repeats by mean callback time.
            runs = {engine: [] for engine in ENGINES}
            for repeat in range(args.repeats):
                for engine in ENGINES:
                    common = ["--engine", engine, "--project", project,
                              "--block-size", str(block)]
                    runs[engine].append(run_bench(bench, common + [
                        "--passes", str(args.passes), "--speed", str(args.speed)],
                        args.timeout))

            for engine in ENGINES:
                print("  %-22s %5d  %-9s ..." % (project, block, engine), end="", flush=True)
                result = median_run(runs[engine])
                if result["status"] == "ok":
                    common = ["--engine", engine, "--project", project,
                              "--block-size", str(block)]
                    probe = run_bench(bench, common + ["--latency"], args.timeout)
                    result["latency"] = probe.get("latency") or {
                        "status": "unmeasured", "reason": probe.get("reason", "")}
                result.setdefault("engine", engine)
                result.setdefault("project", project)
                result.setdefault("block_size", block)
                results.append(result)
                print(" " + result["status"] + (
                    ": " + result.get("reason", "") if result["status"] != "ok" else ""))

    commit, dirty = git_state()
    return {
        "schema": SCHEMA,
        "time": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "commit": commit,
        "dirty": dirty,
        "settings": {"build": listing["build"], "sample_rate": listing["sample_rate"],
                     "passes": args.passes, "speed": args.speed, "repeats": args.repeats},
        "busy": busy,
        "results": results,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__.strip().splitlines()[0])
    parser.add_argument("--bench", help="path to magda_parity_bench")
    parser.add_argument("--bench-dir", default="cmake-build-parity",
                        help="build tree to find it in (default: cmake-build-parity)")
    parser.add_argument("--projects", nargs="+", help="corpus projects to run (default: all)")
    parser.add_argument("--block-sizes", type=lambda s: [int(v) for v in s.split(",")],
                        default=[128, 512], help="comma separated (default: 128,512)")
    parser.add_argument("--passes", type=int, default=2,
                        help="timed passes over each project's window, after one warm-up")
    parser.add_argument("--repeats", type=int, default=3,
                        help="processes per cell, engines alternating; the median is kept")
    parser.add_argument("--allow-busy", action="store_true",
                        help="measure even while another process is using the CPU")
    parser.add_argument("--speed", type=float, default=1.0,
                        help="callback pace as a multiple of real time (default: 1)")
    parser.add_argument("--history-dir", help="default: $MAGDA_PARITY_HISTORY or ~/.magda-parity")
    parser.add_argument("--timeout", type=int, default=900, help="seconds per measurement")
    parser.add_argument("--report", action="store_true",
                        help="judge this machine's last recorded run without measuring")
    args = parser.parse_args()

    thresholds = json.loads(THRESHOLDS.read_text())
    machine = describe_machine()
    path = history_path(args, machine)
    history = read_history(path)

    if args.report:
        if not history:
            sys.exit("no recorded run for %s in %s" % (machine["id"], path))
        run = history[-1]
    else:
        run = measure(args, find_bench(args.bench, args.bench_dir))
        run["machine"] = machine
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open("a") as out:
            out.write(json.dumps(run, sort_keys=True) + "\n")
        history.append(run)
        print("recorded in %s" % path)

    sys.exit(0 if print_report(run, history, thresholds) else 1)


if __name__ == "__main__":
    main()
