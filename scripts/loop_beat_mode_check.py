#!/usr/bin/env python3
"""Check looping and beat mode against a running MAGDA (#3060).

Only beat mode loops. LOOP on a free clip wraps it at the tempo it is heard at, a session
slot is always in beat mode, a tempo detected later replaces the assumed one, and a free
clip that loops in an old project comes back in beat mode sounding as it did. Whether a
detection landing mid-playback is audible is a listening check, not covered here.

Manual setup, once: MAGDA running with MCP on and the edit, transport and session scopes
granted to claude-code. It starts a new project, so save the open one first; --discard
throws its changes away instead.

    python3 scripts/loop_beat_mode_check.py [--discard]
"""

import json
import pathlib
import sys
import tempfile
import time
import zlib

from build_smoke_projects import LOOP, wait_for_job
from build_synthstack_session import Magda

FAILURES = []


def check(label, ok, detail=""):
    print(f"{'PASS' if ok else 'FAIL'}  {label}{f'  ({detail})' if detail else ''}", flush=True)
    if not ok:
        FAILURES.append(label)


def near(a, b, tolerance=1e-3):
    return abs(a - b) <= tolerance


def refused(magda, name, args):
    """The error a call ends with, or None when it succeeds."""
    try:
        magda.call(name, args)
    except SystemExit as error:
        return str(error)
    return None


def audio_of(magda, clip):
    return magda.call("clips.get", {"clipId": clip})["audio"]


def load(magda, track, placement):
    return magda.call("clips.loadSample", {"samplePath": str(LOOP),
                                           "placement": {"trackId": track, **placement}})["id"]


def arrangement(magda):
    tempo = magda.call("project.get")["tempo"]
    track = magda.call("tracks.create", {"name": "Arrangement", "type": "audio"})["id"]
    clip = load(magda, track, {"view": "arrangement", "startBeat": 0.0})
    before = magda.call("clips.get", {"clipId": clip})
    if "audio" not in before:
        raise SystemExit("this MAGDA predates the clips' audio fields: run the #3060 build")
    check("a dropped clip is free and does not loop",
          before["audio"]["playback"] == "free" and not before["audio"]["looping"])

    looped = magda.call("clips.updateAudio", {"clipId": clip, "looping": True})
    audio = looped["audio"]
    check("LOOP puts a free clip in beat mode", audio["playback"] == "beat" and audio["looping"])
    # Kept at the assumed tempo; a detection that already answered refits it to the same file.
    expected = before["lengthBeats"] * (audio["tempo"] / tempo if audio["tempoFrom"] == "analysis"
                                        else 1.0)
    check("LOOP keeps the clip's length, or refits it to a detected tempo",
          near(looped["lengthBeats"], expected),
          f"{before['lengthBeats']:.3f} -> {looped['lengthBeats']:.3f} at {audio['tempo']:.2f}")
    # The assumption, unless a detection has already answered for the file.
    check("a file with no tempo takes the project's",
          (audio["tempoFrom"] == "project" and near(audio["tempo"], tempo))
          or audio["tempoFrom"] == "analysis", f"{audio['tempo']:.2f} from {audio['tempoFrom']}")

    unlooped = magda.call("clips.updateAudio", {"clipId": clip, "playback": "free"})
    check("leaving beat mode ends the loop", not unlooped["audio"]["looping"])

    error = refused(magda, "clips.updateAudio",
                    {"clipId": clip, "playback": "free", "looping": True})
    check("looping a free clip that stays free is refused", error is not None)

    beat = magda.call("clips.updateAudio", {"clipId": clip, "playback": "beat"})
    check("beat mode on its own does not loop",
          beat["audio"]["playback"] == "beat" and not beat["audio"]["looping"])


def session(magda):
    track = magda.call("tracks.create", {"name": "Session", "type": "audio"})["id"]
    scene = magda.call("session.get")["scenes"][0]["id"]
    clip = load(magda, track, {"view": "session", "sceneId": scene, "occupiedPolicy": "replace"})
    audio = audio_of(magda, clip)
    check("a slot drop is in beat mode and loops", audio["playback"] == "beat" and audio["looping"])

    # The file is a 100 bpm loop: the detection asked for at the drop should land.
    deadline = time.time() + 20.0
    while audio["tempoFrom"] == "project" and time.time() < deadline:
        time.sleep(0.5)
        audio = audio_of(magda, clip)
    check("the detected tempo replaces the assumed one", audio["tempoFrom"] == "analysis",
          f"{audio['tempo']:.2f} from {audio['tempoFrom']}")

    check("a slot refuses free playback",
          refused(magda, "clips.updateAudio", {"clipId": clip, "playback": "free"}) is not None)
    check("a slot refuses to stop looping",
          refused(magda, "clips.updateAudio", {"clipId": clip, "looping": False}) is not None)


def old_project(magda, folder):
    """A free clip that loops, as a build before #3060 saved it, opened again."""
    tempo = magda.call("project.get")["tempo"]
    target = folder / "old-free-loop.mgd"
    job = magda.call("project.saveAs", {"path": str(target), "overwritePolicy": "replace",
                                        "mediaPolicy": "copy"})
    written = pathlib.Path(wait_for_job(magda, job)["result"]["project"]["path"])

    project = json.loads(zlib.decompress(written.read_bytes()))
    edited = 0
    for clip in project["clips"]:
        # Saved as numbers: view 0 is the arrangement; only audio clips carry events.
        if clip.get("view") != 0 or not isinstance(clip.get("audio"), dict):
            continue
        clip["loopEnabled"] = True
        for event in clip["audio"]["events"]:
            event.update({"autoTempo": False, "playbackIntent": "free", "speedRatio": 1.25,
                          "interpBpm": 0.0, "bpmFrom": "none"})
        edited += 1
    written.write_bytes(zlib.compress(json.dumps(project).encode()))
    check("the old project holds a free clip that loops", edited > 0)

    job = magda.call("project.open", {"path": str(written), "dirtyPolicy": "discard",
                                      "autosavePolicy": "ignore", "missingMediaPolicy": "fail",
                                      "unavailableDevicePolicy": "allow"})
    wait_for_job(magda, job)
    clips = [c for c in magda.call("clips.list", {})["items"] if c["type"] == "audio"
             and c["view"] == "arrangement"]
    audio = clips[0]["audio"] if clips else None
    check("an old free loop opens in beat mode", audio is not None
          and audio["playback"] == "beat" and audio["looping"])
    # Beat mode reads at projectBpm / interpBpm: 1.25 again, with the speed itself at 1.
    check("and plays at the speed it had", audio is not None and near(audio["speedRatio"], 1.0)
          and audio["tempo"] > 0 and near(tempo / audio["tempo"], 1.25),
          f"tempo {audio['tempo']:.2f}, speed {audio['speedRatio']}" if audio else "")


def main():
    magda = Magda()
    magda.call("project.new", {"discardUnsavedChanges": "--discard" in sys.argv[1:]})
    with tempfile.TemporaryDirectory(prefix="magda-loop-check-") as folder:
        arrangement(magda)
        session(magda)
        old_project(magda, pathlib.Path(folder))
        magda.call("project.new", {"discardUnsavedChanges": True})

    print(f"\n{len(FAILURES)} failed" if FAILURES else "\nall passed")
    sys.exit(1 if FAILURES else 0)


if __name__ == "__main__":
    main()
