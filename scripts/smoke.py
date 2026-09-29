#!/usr/bin/env python3
"""Smoke run for the install test (#2784): open, play and check every project in tests/smoke.

Launches the installed MAGDA (or attaches to one already running), drives it over the
WebSocket Remote API, and for each project: opens it, captures the master output while it
plays the expectation's range, renders the same range offline, then checks the track meters,
compares capture against render, and reads the engine's health. Standard library only.

Usage:
  python3 scripts/smoke.py                          # the installed app, every project
  python3 scripts/smoke.py sidechain drum-grid
  python3 scripts/smoke.py --app path/to/MAGDA      # a specific build
  python3 scripts/smoke.py --attach                 # a MAGDA already running, left running
"""

import argparse
import array
import datetime
import json
import math
import os
import platform
import shutil
import struct
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools" / "transport_check"))
sys.path.insert(0, str(ROOT / "scripts"))
from clients import WsClient  # noqa: E402
from discovery import config_file, data_dir, find_records, os_default_app_data_dir  # noqa: E402
from smoke_machine import describe_machine, git_state  # noqa: E402

CLIENT = "magda-smoke"
SCOPES = ("edit", "transport", "session")
ENGINE = "magda::engine"
SILENT_DB = -90.0
SCHEMA = 1

# Capture-versus-render thresholds. The two are separate passes, so they are compared on
# block energy and alignment rather than nulled sample by sample.
BLOCK = 256
DROPOUT_DROP_DB = 30.0
AUDIBLE_DB = -50.0
HOP = 32
ENV_WINDOW = 1024
ALIGN_SEARCH_HOPS = 64
DRIFT_HOPS = 128
DRIFT_MIN_HOPS = 4
DRIFT_WINDOWS = 3
MIN_MATCH = 0.95
MIN_SILENT_RUN = 32
SILENCE_MARGIN = 64
DURATION_TOLERANCE = 0.01
PARAMETER_TOLERANCE = 0.005
POLL_SECONDS = 0.08


class SmokeError(Exception):
    pass


def items(result):
    """A listing's entries: MCP wraps them in `items`, the WebSocket returns them bare."""
    return result["items"] if isinstance(result, dict) else result


def db(linear):
    return 20.0 * math.log10(linear) if linear > 0 else -math.inf


def fmt_db(value):
    return "-inf" if value == -math.inf else "%.1f" % value


# --- MAGDA ------------------------------------------------------------------------------


def default_app():
    system = platform.system()
    if system == "Darwin":
        return Path("/Applications/MAGDA.app/Contents/MacOS/MAGDA")
    if system == "Windows":
        base = os.environ.get("ProgramFiles", r"C:\Program Files")
        return Path(base) / "MAGDA" / "MAGDA.exe"
    for folder in (Path.home() / "Applications", Path.home(), Path("/opt")):
        found = sorted(folder.glob("MAGDA*.AppImage")) if folder.is_dir() else []
        if found:
            return found[-1]
    return Path("magda")


class Magda:
    """A connection to one running MAGDA, and the process when this script started it."""

    def __init__(self, process, record, timeout):
        self.process = process
        self.record = record
        self.ws = WsClient("127.0.0.1", record.port, record.token, client_name=CLIENT,
                           timeout=timeout)
        self.ws.connect()

    def call(self, method, params=None, timeout=None):
        for _ in range(50):
            reply = self.ws.call(method, params or {}, timeout=timeout)
            if reply.ok:
                return reply.result
            error = reply.error or {}
            # Refused at admission, so it never ran and is safe to send again.
            if reply.raw.get("id") is not None or "rate limit" not in error.get("message", ""):
                break
            time.sleep(0.1)
        raise SmokeError("%s: %s" % (method, error.get("message", error)))

    def wait_ready(self, timeout=60):
        """Wait out startup: the first project swap cancels requests and may drop the socket."""
        deadline = time.monotonic() + timeout
        while True:
            try:
                if self.call("engine.health")["projectBound"]:
                    return
            except (SmokeError, OSError, TimeoutError) as error:
                if time.monotonic() > deadline:
                    raise SmokeError("MAGDA never became ready: %s" % error)
                self.ws.close()
                self.ws.connect()
            if time.monotonic() > deadline:
                raise SmokeError("MAGDA never bound a project")
            time.sleep(0.5)

    def ensure_grants(self):
        """Wait for the user to grant this client what the run uses, asking once for all of it."""
        missing = [s for s in SCOPES if s not in granted_scopes()]
        if not missing:
            return
        # Calls that change nothing even when allowed; a denied one raises MAGDA's grant sheet.
        ghost = max([t["id"] for t in items(self.call("tracks.list"))] + [0]) + 1000
        loop = self.call("transport.get")["loopEnabled"]
        probes = {"edit": ("tracks.delete", {"trackId": ghost}),
                  "transport": ("transport.setLoopEnabled", {"enabled": loop}),
                  "session": ("session.stopTrack", {"trackId": ghost})}
        for scope in missing:
            self.ws.call(*probes[scope])
        print("Grant %s to %s in the sheet MAGDA shows; later runs will not ask." % (
            ", ".join(missing), CLIENT), flush=True)
        deadline = time.monotonic() + 300
        while time.monotonic() < deadline:
            time.sleep(1)
            if all(s in granted_scopes() for s in SCOPES):
                # MAGDA drops a socket left idle this long.
                self.ws.close()
                self.ws.connect()
                return
        raise SmokeError("%s was not granted %s: grant it in Settings -> Connections -> "
                         "Clients" % (CLIENT, ", ".join(missing)))

    def job(self, started, timeout=600):
        """Poll a job to its end; the finished job, or SmokeError with its message."""
        job = started
        deadline = time.monotonic() + timeout
        while job["state"] in ("accepted", "running"):
            if time.monotonic() > deadline:
                self.call("jobs.cancel", {"jobId": job["id"]})
                raise SmokeError("%s timed out" % job["kind"])
            time.sleep(0.1)
            try:
                job = self.call("jobs.get", {"jobId": job["id"]})
            except SmokeError as error:
                # A project swap cancels requests queued across it; the job itself carries on.
                if "cancelled before execution" not in str(error):
                    raise
        if job["state"] != "completed":
            error = job.get("error") or {}
            raise SmokeError("%s %s: %s" % (job["kind"], job["state"],
                                             error.get("message", "no reason given")))
        return job

    def close(self, quit_app):
        try:
            if quit_app:
                self.call("project.close", {"discardUnsavedChanges": True})
        except (SmokeError, OSError, TimeoutError):
            pass
        self.ws.close()
        if quit_app and self.process is not None:
            self.process.terminate()
            try:
                self.process.wait(30)
            except subprocess.TimeoutExpired:
                self.process.kill()


def granted_scopes():
    try:
        config = json.loads(config_file(os_default_app_data_dir()).read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return set()
    for client in (config.get("remoteApi") or {}).get("clients", []):
        if client.get("name") == CLIENT:
            return set(client.get("scopes", []))
    return set()


def live_record(pid=None):
    for record in find_records(data_dir()):
        if pid is None or record.pid == pid:
            return record
    return None


def connect(args):
    """Attach to a running MAGDA, or launch one and wait for its WebSocket."""
    running = live_record()
    if args.attach or (running is not None and args.app is None):
        if running is None:
            raise SmokeError("no running MAGDA found in %s" % data_dir())
        if not running.has_websocket:
            raise SmokeError("MAGDA is running with its WebSocket off: switch on "
                             "Settings -> Connections -> WebSocket")
        return Magda(None, running, args.timeout)

    app = Path(args.app) if args.app else default_app()
    env = dict(os.environ)
    try:
        process = subprocess.Popen([str(app)], env=env, stdout=subprocess.DEVNULL,
                                   stderr=subprocess.DEVNULL)
    except OSError as error:
        raise SmokeError("could not launch %s: %s (give the app with --app)" % (app, error))
    print("launched %s (pid %d)" % (app, process.pid), flush=True)
    deadline = time.monotonic() + 120
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise SmokeError("MAGDA exited during startup with code %d" % process.returncode)
        record = live_record(process.pid)
        if record is not None and record.has_websocket:
            return Magda(process, record, args.timeout)
        time.sleep(0.5)
    process.terminate()
    raise SmokeError("MAGDA published no WebSocket within two minutes: switch on "
                     "Settings -> Connections -> WebSocket, then run again")


# --- audio files ------------------------------------------------------------------------


def read_wav(path):
    """(sample rate, mono float samples) from a PCM or float WAV."""
    data = Path(path).read_bytes()
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise SmokeError("%s is not a WAV file" % path)
    offset, fmt, frames = 12, None, None
    while offset + 8 <= len(data):
        chunk, size = data[offset:offset + 4], struct.unpack_from("<I", data, offset + 4)[0]
        body = data[offset + 8:offset + 8 + size]
        if chunk == b"fmt ":
            tag, channels, rate = struct.unpack_from("<HHI", body)
            bits = struct.unpack_from("<H", body, 14)[0]
            if tag == 0xFFFE:
                tag = struct.unpack_from("<H", body, 24)[0]
            fmt = (tag, channels, rate, bits)
        elif chunk == b"data":
            frames = body
        offset += 8 + size + (size & 1)
    if fmt is None or frames is None:
        raise SmokeError("%s has no fmt or data chunk" % path)
    tag, channels, rate, bits = fmt
    if tag == 3 and bits == 32:
        samples = array.array("f")
        samples.frombytes(frames[:len(frames) - len(frames) % 4])
    elif tag == 1 and bits == 16:
        raw = array.array("h")
        raw.frombytes(frames[:len(frames) - len(frames) % 2])
        samples = array.array("f", (s / 32768.0 for s in raw))
    elif tag == 1 and bits == 32:
        raw = array.array("i")
        raw.frombytes(frames[:len(frames) - len(frames) % 4])
        samples = array.array("f", (s / 2147483648.0 for s in raw))
    else:
        raise SmokeError("%s: unsupported WAV format %d/%d-bit" % (path, tag, bits))
    if sys.byteorder != "little":
        samples.byteswap()
    if channels == 1:
        return rate, samples
    mono = array.array("f", bytes(4 * (len(samples) // channels)))
    for c in range(channels):
        channel = samples[c::channels]
        for i in range(len(mono)):
            mono[i] += channel[i] / channels
    return rate, mono


def energy_prefix(samples):
    """Running sum of squares, so any window's energy is one subtraction."""
    prefix = array.array("d", [0.0])
    total = 0.0
    for sample in samples:
        total += sample * sample
        prefix.append(total)
    return prefix


def window_db(prefix, start, count):
    if start < 0 or start + count >= len(prefix):
        return -math.inf
    return db(math.sqrt(max(prefix[start + count] - prefix[start], 0.0) / count))


def envelope(prefix):
    """RMS every HOP samples over ENV_WINDOW; phase-blind, so it matches across two passes."""
    last = len(prefix) - 1 - ENV_WINDOW
    return [math.sqrt(max(prefix[i + ENV_WINDOW] - prefix[i], 0.0) / ENV_WINDOW)
            for i in range(0, max(last, 0), HOP)]


def pearson(a, b):
    n = len(a)
    mean_a, mean_b = sum(a) / n, sum(b) / n
    cov = sum((x - mean_a) * (y - mean_b) for x, y in zip(a, b))
    spread = math.sqrt(sum((x - mean_a) ** 2 for x in a) * sum((y - mean_b) ** 2 for y in b))
    return cov / spread if spread else 0.0


def envelope_lag(reference, other, start, length, centre, search):
    """The hop shift of other against reference[start:start+length] that correlates best."""
    best, best_score = 0, -math.inf
    window = reference[start:start + length]
    for lag in range(centre - search, centre + search + 1):
        begin = start + lag
        if begin < 0 or begin + length > len(other):
            continue
        candidate = other[begin:begin + length]
        norm = math.sqrt(sum(y * y for y in candidate)) or 1.0
        score = sum(x * y for x, y in zip(window, candidate)) / norm
        if score > best_score:
            best, best_score = lag, score
    return best


def digital_silence(render_prefix, capture, offset):
    """(render sample, length) of the first run of exact zeros where the render sounds."""
    run_start, run = 0, 0
    for i, sample in enumerate(capture):
        if sample == 0.0:
            if run == 0:
                run_start = i
            run += 1
            continue
        # The margin absorbs a live trigger landing a few samples off the render's.
        if run >= MIN_SILENT_RUN + 2 * SILENCE_MARGIN:
            at = run_start - offset
            inner = run - 2 * SILENCE_MARGIN
            if all(window_db(render_prefix, at + SILENCE_MARGIN + k, MIN_SILENT_RUN) >= AUDIBLE_DB
                   for k in range(0, inner - MIN_SILENT_RUN + 1, MIN_SILENT_RUN)):
                return at, run
        run = 0
    return None


def compare(render, capture):
    """Findings from capture against render, each at the render sample it happened.

    Live and offline passes do not null (free-running oscillators start anywhere), so
    they are aligned and compared on their energy envelopes, window by window.
    """
    render_prefix, capture_prefix = energy_prefix(render), energy_prefix(capture)
    render_env, capture_env = envelope(render_prefix), envelope(capture_prefix)
    loudest = max(render_env, default=0.0)
    if db(loudest) < SILENT_DB:
        return {"offset": None, "findings": ["render is silent"]}
    onset_render = next(i for i, v in enumerate(render_env) if v >= loudest * 0.1)
    onset_capture = next((i for i, v in enumerate(capture_env) if v >= loudest * 0.1), None)
    if onset_capture is None:
        return {"offset": None, "findings": ["capture is silent"]}
    coarse = onset_capture - onset_render

    # A window with no movement in it aligns anywhere; only the ones with some pin it down.
    lags = []
    for start in range(0, len(render_env) - DRIFT_HOPS, DRIFT_HOPS):
        window = render_env[start:start + DRIFT_HOPS]
        if db(max(window)) < AUDIBLE_DB or db(max(window)) - db(max(min(window), 1e-9)) < 3.0:
            continue
        lag = envelope_lag(render_env, capture_env, start, DRIFT_HOPS, coarse, ALIGN_SEARCH_HOPS)
        matched = capture_env[start + lag:start + lag + DRIFT_HOPS]
        if len(matched) == DRIFT_HOPS and pearson(window, matched) >= MIN_MATCH:
            lags.append((start, lag))
    if not lags:
        return {"offset": None, "findings": ["render has no dynamics to align on"]}
    offset = sorted(lag for _, lag in lags[:3])[len(lags[:3]) // 2]
    offset_samples = offset * HOP

    findings = []
    dropout = digital_silence(render_prefix, capture, offset_samples)
    if dropout is not None:
        findings.append("dropout at sample %d: %d samples of digital silence" % dropout)
    else:
        # Two blocks, so a live trigger landing a few samples late is not a dropout.
        quiet = 0
        for start in range(0, len(render) - BLOCK, BLOCK):
            wanted = window_db(render_prefix, start, BLOCK)
            inside = 0 <= start + offset_samples < len(capture) - BLOCK
            got = window_db(capture_prefix, start + offset_samples, BLOCK) if inside else 0.0
            quiet = quiet + 1 if wanted >= AUDIBLE_DB and got < wanted - DROPOUT_DROP_DB else 0
            if quiet == 2:
                findings.append("dropout at sample %d (%s dB where the render has %s)" % (
                    start - BLOCK, fmt_db(got), fmt_db(wanted)))
                break

    # A skip shifts everything after it; a smeared transient only moves its own window.
    for i in range(1, len(lags) - DRIFT_WINDOWS + 1):
        later = sorted(lag for _, lag in lags[i:])
        moved = later[len(later) // 2] - offset
        if abs(moved) >= DRIFT_MIN_HOPS and abs(lags[i][1] - offset) >= DRIFT_MIN_HOPS:
            findings.append("alignment moves by %d samples by sample %d" % (
                moved * HOP, lags[i][0] * HOP))
            break
    return {"offset": offset_samples, "findings": findings}


# --- one project ------------------------------------------------------------------------


def load_expectations(smoke_dir, names):
    projects = []
    for path in sorted(Path(smoke_dir).glob("*/*.smoke.json")):
        name = path.parent.name
        if names and name not in names:
            continue
        spec = json.loads(path.read_text(encoding="utf-8"))
        projects.append((name, path.parent / spec["project"], spec))
    missing = set(names) - {name for name, _, _ in projects}
    if missing:
        raise SmokeError("no smoke project named %s" % ", ".join(sorted(missing)))
    return projects


def unmet_requirement(magda, spec, hardware):
    requires = spec.get("requires", {})
    wanted = requires.get("hardware", [])
    lacking = [h for h in wanted if h not in hardware]
    if lacking:
        return "needs hardware: " + ", ".join(lacking)
    plugins = requires.get("plugins", [])
    if plugins:
        catalog = {d["name"] for d in items(magda.call("devices.catalog"))}
        absent = [p for p in plugins if p not in catalog]
        if absent:
            return "needs plugins: " + ", ".join(absent)
    return None


def check_parameters(magda, spec, track_ids):
    """Parameters a restored plugin state must come back with, off the live instance."""
    failures = []
    for check in spec.get("parameters", []):
        track_id = track_ids.get(check["track"])
        devices = magda.call("devices.list", {"trackId": track_id})["devices"] if track_id else []
        device = next((d for d in devices if d["name"] == check["device"]), None)
        if device is None:
            failures.append("%s: no device %s" % (check["track"], check["device"]))
            continue
        # A hosted plugin describes itself once its instance has loaded.
        deadline = time.monotonic() + 15
        described = []
        while not described and time.monotonic() < deadline:
            described = items(magda.call("devices.listParameters",
                                         {"devicePath": device["devicePath"]}))
            time.sleep(0.2)
        values = {p["name"]: p["normalizedValue"] for p in described}
        for name, wanted in check["normalized"].items():
            got = values.get(name)
            if got is None or abs(got - wanted) > PARAMETER_TOLERANCE:
                failures.append("%s %s: %s is %s, the expectation wants %s" % (
                    check["track"], check["device"], name,
                    "missing" if got is None else "%.4f" % got, wanted))
    return failures


def play_range(magda, spec, track_ids):
    """Play the range with its scenario; the peak per track id and the load samples."""
    start, end = spec["range"]["startBeat"], spec["range"]["endBeat"]
    scenario = sorted(spec.get("scenario", []), key=lambda step: step["beat"])
    peaks = {track_id: 0.0 for track_id in track_ids}
    loads = []
    magda.call("transport.seek", {"positionBeats": start})
    magda.call("transport.play")
    deadline = time.monotonic() + 600
    position = start
    polls = 0
    while position < end:
        if time.monotonic() > deadline:
            raise SmokeError("playback never reached beat %g" % end)
        while scenario and scenario[0]["beat"] <= position:
            step = scenario.pop(0)
            magda.call(step["call"], step.get("input", {}))
        meters = magda.call("meters.read")
        for track in meters["tracks"]:
            if track["available"] and track["trackId"] in peaks:
                peaks[track["trackId"]] = max(peaks[track["trackId"]], track["peakL"],
                                              track["peakR"])
        if polls % 10 == 0:
            load = magda.call("engine.health")["callbackLoad"]
            if load is not None:
                loads.append(load)
        polls += 1
        position = magda.call("transport.get")["positionBeats"]
        # Two requests a pass keeps well inside the WebSocket's 50 per second.
        time.sleep(POLL_SECONDS)
    return peaks, loads


def check_meters(spec, track_ids, peaks):
    failures = []
    for track in spec["tracks"]:
        track_id = track_ids.get(track["name"])
        if track_id is None:
            failures.append("%s: no such track" % track["name"])
            continue
        peak = db(peaks.get(track_id, 0.0))
        bounds = track.get("peakDb", {})
        if track["sound"]:
            low, high = bounds.get("min", -math.inf), bounds.get("max", 0.0)
            if not low <= peak <= high:
                failures.append("%s peaked at %s dB, wanted %g..%g" % (
                    track["name"], fmt_db(peak), low, high))
        elif peak >= bounds.get("max", SILENT_DB):
            failures.append("%s should be silent but peaked at %s dB" % (
                track["name"], fmt_db(peak)))
    return failures


def run_project(magda, name, mgd, spec, out_dir):
    result = {"project": name, "status": "pass", "failures": [], "listen": spec.get("listen", "")}
    started = time.monotonic()
    # A copy, so a run never writes into the repository (a freeze render, an autosave),
    # and every run proves the project's media moves with it.
    copy = out_dir / "projects" / mgd.parent.name
    shutil.copytree(mgd.parent, copy)
    magda.job(magda.call("project.open", {
        "path": str((copy / mgd.name).resolve()), "dirtyPolicy": "discard", "autosavePolicy": "ignore",
        "missingMediaPolicy": "fail", "unavailableDevicePolicy": "fail"}))
    result["loadSeconds"] = round(time.monotonic() - started, 2)

    for step in spec.get("setup", []):
        done = magda.call(step["call"], step.get("input", {}))
        if isinstance(done, dict) and done.get("state") in ("accepted", "running"):
            magda.job(done)
    track_ids = {t["name"]: t["id"] for t in items(magda.call("tracks.list"))}
    result["failures"] += check_parameters(magda, spec, track_ids)
    magda.call("transport.stop")
    before = magda.call("engine.health")
    capture_path = out_dir / (name + ".capture.wav")
    capture = magda.call("engine.masterCapture.start", {
        "path": str(capture_path), "format": "wav", "bitDepth": 32,
        "overwritePolicy": "replace"})
    if capture["state"] not in ("accepted", "running"):
        raise SmokeError("master capture %s" % capture["state"])
    deadline = time.monotonic() + 5
    while not magda.call("engine.masterCapture.status")["active"]:
        if time.monotonic() > deadline:
            raise SmokeError("master capture never became active")
        time.sleep(0.05)
    # The request is live; the audio thread picks it up on its next callback.
    time.sleep(0.2)
    try:
        peaks, loads = play_range(magda, spec, list(track_ids.values()))
    finally:
        magda.job(magda.call("engine.masterCapture.stop", {"jobId": capture["id"]}))
        magda.call("transport.stop")
    after = magda.call("engine.health")

    result["failures"] += check_meters(spec, track_ids, peaks)
    xruns = (after["xrunCount"] or 0) - (before["xrunCount"] or 0)
    result["xruns"] = xruns
    if xruns > 0:
        result["failures"].append("%d xrun(s) during playback" % xruns)
    result["meanLoad"] = round(sum(loads) / len(loads), 3) if loads else None
    result["peakLoad"] = round(max(loads), 3) if loads else None

    if spec.get("compare") is False:
        result["compare"] = "skipped: live and offline differ by design here"
    elif spec.get("scenario"):
        # A scenario acts on live playback only: a launched scene, a bypass mid-range.
        result["compare"] = "skipped: a scenario changes what plays live"
    else:
        rate, captured = read_wav(capture_path)
        render_path = out_dir / (name + ".render.wav")
        magda.job(magda.call("engine.renderRange", {
            "path": str(render_path),
            "range": {"unit": "beats", "start": spec["range"]["startBeat"],
                      "end": spec["range"]["endBeat"]},
            "format": "wav", "sampleRate": rate, "bitDepth": 32, "dither": "none",
            "normalise": False, "normaliseToDb": 0, "includeMasterEffects": True,
            "includeTrackEffects": True, "realTime": False, "tailSeconds": 0,
            "overwritePolicy": "replace"}))
        _, rendered = read_wav(render_path)
        wanted = spec["range"].get("seconds")
        took = len(rendered) / rate
        if wanted is not None and abs(took - wanted) > wanted * DURATION_TOLERANCE:
            result["failures"].append("the range took %.3f s, wanted %.3f s" % (took, wanted))
        comparison = compare(rendered, captured)
        result["compare"] = "offset %s" % comparison["offset"]
        result["failures"] += comparison["findings"]

    if result["failures"]:
        result["status"] = "fail"
    return result


# --- history and report -----------------------------------------------------------------


def history_root(args, machine):
    root = Path(args.history_dir or os.environ.get("MAGDA_SMOKE_HISTORY")
                or Path.home() / ".magda-smoke")
    return root / machine["id"]


def previous_run(root):
    path = root / "history.jsonl"
    if not path.exists():
        return None
    lines = [line for line in path.read_text().splitlines() if line.strip()]
    return json.loads(lines[-1]) if lines else None


def print_report(run, earlier):
    print()
    print("smoke run %s on %s, %s" % (run["time"], run["machine"]["id"],
                                      run["commit"][:10] or "no git"))
    print("%-18s %-7s %8s %8s %8s %6s" % ("project", "result", "load s", "mean cpu",
                                         "peak cpu", "xruns"))
    for r in run["results"]:
        if r["status"] == "skip":
            print("%-18s %-7s %s" % (r["project"], "skip", r["reason"]))
            continue
        print("%-18s %-7s %8s %8s %8s %6s" % (
            r["project"], r["status"], r.get("loadSeconds", "-"), r.get("meanLoad", "-"),
            r.get("peakLoad", "-"), r.get("xruns", "-")))
        for failure in r["failures"]:
            print("    - " + failure)
    if earlier is None:
        print("\nfirst run on this machine")
        return
    print("\nsince %s:" % earlier["time"])
    before = {r["project"]: r for r in earlier["results"]}
    changed = False
    for r in run["results"]:
        old = before.get(r["project"])
        if old is None:
            print("  %s: new" % r["project"])
            changed = True
            continue
        notes = []
        if old["status"] != r["status"]:
            notes.append("%s -> %s" % (old["status"], r["status"]))
        for key, label in (("loadSeconds", "load"), ("meanLoad", "mean cpu"),
                           ("xruns", "xruns")):
            a, b = old.get(key), r.get(key)
            if a is not None and b is not None and a != b:
                notes.append("%s %s -> %s" % (label, a, b))
        if notes:
            print("  %s: %s" % (r["project"], ", ".join(notes)))
            changed = True
    if not changed:
        print("  no change")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("projects", nargs="*", help="project names; default: all")
    parser.add_argument("--app", help="the MAGDA executable; default: the installed one")
    parser.add_argument("--attach", action="store_true",
                        help="use the MAGDA already running and leave it running")
    parser.add_argument("--set", default=str(ROOT / "tests" / "smoke"), dest="smoke_dir")
    parser.add_argument("--hardware", default="",
                        help="hardware this machine has, comma separated: insert,loopback")
    parser.add_argument("--history-dir", help="default: $MAGDA_SMOKE_HISTORY or ~/.magda-smoke")
    parser.add_argument("--timeout", type=float, default=30.0)
    args = parser.parse_args()

    projects = load_expectations(args.smoke_dir, args.projects)
    machine = describe_machine()
    root = history_root(args, machine)

    magda = connect(args)
    quit_app = magda.process is not None
    results = []
    try:
        magda.wait_ready()
        magda.ensure_grants()
        stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
        out_dir = root / stamp
        out_dir.mkdir(parents=True, exist_ok=True)
        engine = magda.call("engine.health")["engine"]
        if engine != ENGINE:
            raise SmokeError("MAGDA is running %s: switch it to the magda engine in "
                             "Settings -> Audio, or run without --attach" % engine)
        hardware = {h for h in args.hardware.split(",") if h}
        for name, mgd, spec in projects:
            reason = unmet_requirement(magda, spec, hardware)
            if reason:
                results.append({"project": name, "status": "skip", "reason": reason})
                continue
            print("%s ..." % name, flush=True)
            try:
                results.append(run_project(magda, name, mgd, spec, out_dir))
            except (SmokeError, OSError, TimeoutError) as error:
                results.append({"project": name, "status": "fail",
                                "failures": ["%s: %s" % (type(error).__name__, error)]})
    finally:
        magda.close(quit_app)

    earlier = previous_run(root)
    commit, dirty = git_state()
    run = {"schema": SCHEMA, "time": stamp, "machine": machine,
           "commit": commit, "dirty": dirty, "results": results}
    (out_dir / "result.json").write_text(json.dumps(run, indent=2))
    with (root / "history.jsonl").open("a") as history:
        history.write(json.dumps(run) + "\n")
    print_report(run, earlier)
    print("\nfiles: %s" % out_dir)
    return 1 if any(r["status"] == "fail" for r in results) else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except SmokeError as error:
        sys.exit("smoke: %s" % error)
