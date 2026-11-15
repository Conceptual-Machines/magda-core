#!/usr/bin/env python3
"""Play load projects in a running MAGDA and report the audio callback's load and xruns (#1898).

The buffer size is set by hand in Settings; --label names the setup in the results, which are
appended to local/load/results.jsonl. Start MAGDA with MCP on, then:

    python3 scripts/measure_engine_load.py --label "512 ahead-off" instrument-load instrument-load-flat

Each project is opened from local/load/<name>/<name>.mgd and played from the start for
--seconds (default 40), sampling engine.health every half second. --discard drops unsaved
changes in the open project.
"""

import argparse
import json
import os
import pathlib
import statistics
import sys
import time

from build_instrument_load_session import OUT_DIR
from build_smoke_projects import wait_for_job
from build_synthstack_session import Magda

SAMPLE_SECONDS = 0.5
# Load right after play starts reflects the stop, not the project.
SETTLE_SECONDS = 3.0


def saved_settings():
    """The audio block as config.json holds it."""
    if sys.platform == "darwin":
        folder = pathlib.Path.home() / "Library"
    elif sys.platform == "win32":
        folder = pathlib.Path(os.environ["APPDATA"])
    else:
        folder = pathlib.Path.home() / ".config"
    config = json.loads((folder / "MAGDA" / "config.json").read_text())
    audio = config.get("audioIO") or {}
    return {"bufferSize": audio.get("bufferSize"), "sampleRate": audio.get("sampleRate")}


def open_project(magda, name, discard):
    path = OUT_DIR / name / f"{name}.mgd"
    wait_for_job(magda, magda.call("project.open", {
        "path": str(path), "dirtyPolicy": "discard" if discard else "fail",
        "autosavePolicy": "ignore", "missingMediaPolicy": "fail",
        "unavailableDevicePolicy": "fail"}))


def measure(magda, seconds):
    magda.call("transport.stop")
    magda.call("transport.seek", {"positionBeats": 0})
    before = magda.call("engine.health").get("xrunCount") or 0
    magda.call("transport.play")
    time.sleep(SETTLE_SECONDS)
    loads = []
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        load = magda.call("engine.health").get("callbackLoad")
        if load is not None:
            loads.append(load)
        time.sleep(SAMPLE_SECONDS)
    after = magda.call("engine.health").get("xrunCount") or 0
    magda.call("transport.stop")
    return {"loadMean": statistics.fmean(loads) if loads else None,
            "loadMax": max(loads) if loads else None, "samples": len(loads),
            "xruns": after - before}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("projects", nargs="+")
    parser.add_argument("--label", required=True)
    parser.add_argument("--seconds", type=float, default=40.0)
    parser.add_argument("--discard", action="store_true")
    parser.add_argument("--select", help="track id to select after opening, or none")
    args = parser.parse_args()

    magda = Magda()
    settings = saved_settings()
    print(f"saved settings: {settings}", flush=True)
    for name in args.projects:
        open_project(magda, name, args.discard)
        if args.select is not None:
            track = None if args.select == "none" else int(args.select)
            magda.call("selection.set", {
                "trackId": track, "clipId": None, "clipIds": [], "automationLaneId": None,
                "automationClipId": None, "noteClipId": None, "noteIndices": []})
        selected = magda.call("selection.get").get("trackId")
        result = {"label": args.label, "project": name, "selectedTrack": selected, **settings,
                  **measure(magda, args.seconds)}
        with open(OUT_DIR / "results.jsonl", "a") as out:
            out.write(json.dumps(result) + "\n")
        mean, peak = (f"{result[k]:.0%}" if result[k] is not None else "n/a"
                      for k in ("loadMean", "loadMax"))
        print(f"{args.label:16} {name:28} selected {selected}  load mean {mean} max {peak}  "
              f"xruns {result['xruns']}", flush=True)


if __name__ == "__main__":
    main()
