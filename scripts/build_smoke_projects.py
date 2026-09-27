#!/usr/bin/env python3
"""Build the smoke projects in tests/smoke in a running MAGDA, over its MCP endpoint.

Each project is built into a new untitled project and saved over tests/smoke/<name>/<name>.mgd.
The expectation file beside it is written by hand. Start MAGDA with MCP on and the edit scope
granted to claude-code, then:

    python3 scripts/build_smoke_projects.py            # every project
    python3 scripts/build_smoke_projects.py drum-grid  # just the named ones

--discard drops unsaved changes in the open project, such as one a failed build left behind.
"""

import pathlib
import shutil
import sys
import time

from build_synthstack_session import Magda

SMOKE_DIR = pathlib.Path(__file__).resolve().parent.parent / "tests/smoke"


def track_path(track, device_id=None, steps=()):
    return {"trackId": track, "section": "fx", "trackLevel": False,
            "topLevelDeviceId": device_id, "steps": list(steps)}


def add_track(magda, name, *devices):
    """A track with devices in chain order; returns the track id and each device's path."""
    track = magda.call("tracks.create", {"name": name, "type": "audio"})["id"]
    paths = [magda.call("devices.add", {"trackId": track, "catalogId": d})["devicePath"]
             for d in devices]
    return track, paths


def midi_clip(magda, track, length, notes, start=0.0):
    """An arrangement clip holding (note, velocity, beat, length) tuples."""
    clip = magda.call("clips.createMidi", {
        "lengthBeats": length,
        "placement": {"view": "arrangement", "trackId": track, "startBeat": start},
    })["id"]
    add_notes(magda, clip, notes)
    return clip


def add_notes(magda, clip, notes):
    if notes:
        magda.call("clips.addMidiEvents", {"clipId": clip, "events": [
            {"type": "note", "note": n, "velocity": v, "beat": b, "lengthBeats": l}
            for n, v, b, l in notes]})


def parameter_index(magda, path, name):
    """The index of the first parameter whose name contains `name`, ignoring case."""
    for parameter in magda.call("devices.listParameters", {"devicePath": path})["items"]:
        if name.lower() in parameter["name"].lower():
            return parameter["index"]
    raise SystemExit(f"no parameter matching {name!r} on {path}")


def beats(count, step, note, velocity=100, length=None):
    return [(note, velocity, i * step, length or step * 0.5) for i in range(int(count / step))]


def wait_for_job(magda, job):
    while job["state"] in ("accepted", "running"):
        time.sleep(0.1)
        try:
            job = magda.call("jobs.get", {"jobId": job["id"]})
        except SystemExit as error:
            # A project swap cancels requests queued across it; the job carries on.
            if "cancelled before execution" not in str(error):
                raise
    if job["state"] != "completed":
        raise SystemExit(f"job {job['kind']} ended {job['state']}: {job.get('error')}")
    return job


# ---------------------------------------------------------------------------
# Projects
# ---------------------------------------------------------------------------


def faust_devices(magda):
    bass, _ = add_track(magda, "FM Bass", "magda_fm", "magda_saturator")
    midi_clip(magda, bass, 8, [(33, 100, 0, 1.5), (45, 100, 1.5, 0.5), (33, 100, 2, 2),
                               (31, 100, 4, 1.5), (43, 100, 5.5, 0.5), (31, 100, 6, 2)])
    pad, _ = add_track(magda, "Pad", "magda_polysynth", "magda_chorus", "magda_delay",
                       "magda_reverb")
    midi_clip(magda, pad, 8, [(n, 90, 0, 4) for n in (57, 60, 64)] +
              [(n, 90, 4, 4) for n in (55, 59, 62)])
    kick, _ = add_track(magda, "Kick", "magda_kick")
    midi_clip(magda, kick, 8, beats(8, 1, 36, 110))
    snare, _ = add_track(magda, "Snare", "magda_snare")
    midi_clip(magda, snare, 8, [(38, 105, b, 0.5) for b in (1, 3, 5, 7)])
    marimba, _ = add_track(magda, "Muted Marimba", "magda_marimba")
    midi_clip(magda, marimba, 8, [(69, 100, 0, 1), (72, 100, 2, 1), (71, 100, 4, 1),
                                  (67, 100, 6, 1)])
    magda.call("tracks.update", {"trackId": marimba, "muted": True})


def tempo_automation(magda):
    magda.call("project.setTempo", {"tempo": 100})
    hat, _ = add_track(magda, "Hat", "magda_hat")
    midi_clip(magda, hat, 16, beats(16, 0.5, 42))
    synth, _ = add_track(magda, "Synth", "magda_polysynth")
    midi_clip(magda, synth, 16, beats(16, 1, 60, 95, 1))
    lane = magda.call("automation.createLane", {"type": "absolute", "target": {
        "kind": "tempo", "devicePath": None, "parameterIndex": -1, "modId": -1,
        "modParameterIndex": -1, "sendBusIndex": -1}})["id"]
    # The tempo lane spans 20..300 BPM linearly: 100 -> 140 over the first two bars.
    magda.call("automation.setPoints", {"laneId": lane, "points": [
        {"beatPosition": 0, "value": (100 - 20) / 280, "curve": "linear"},
        {"beatPosition": 8, "value": (140 - 20) / 280, "curve": "linear"}]})
    magda.call("project.setLoopRange", {"startBeat": 8, "endBeat": 16})
    magda.call("transport.setLoopEnabled", {"enabled": True})


def session_launcher(magda):
    """Three scenes; the first follows on to the second after two passes."""
    scenes = [s["id"] for s in magda.call("session.get")["scenes"]][:3]
    bass, _ = add_track(magda, "Bass", "magda_fm")
    drums, _ = add_track(magda, "Drums", "magda_kick")
    keys, _ = add_track(magda, "Keys", "magda_polysynth")
    # Follow actions are per slot, so Keys needs a (silent) clip in scene 1 to follow on.
    parts = {
        (bass, 0): [(33, 100, b, 0.9) for b in range(4)],
        (drums, 0): beats(4, 1, 36, 110),
        (keys, 0): [],
        (bass, 1): [(31, 100, b, 0.9) for b in range(4)],
        (keys, 1): [(n, 90, 0, 3.9) for n in (55, 59, 62)],
        (keys, 2): [(n, 90, 0, 3.9) for n in (57, 60, 64)],
    }
    for (track, scene), notes in parts.items():
        clip = magda.call("clips.createMidi", {"lengthBeats": 4, "placement": {
            "view": "session", "trackId": track, "sceneId": scenes[scene],
            "occupiedPolicy": "fail"}})["id"]
        add_notes(magda, clip, notes)
        if scene == 0:
            magda.call("session.updateClipSettings", {
                "clipId": clip, "followAction": "next", "followActionLoopCount": 2})


def rack_modulation(magda):
    """Two instruments in parallel rack chains, one under an LFO, one under a macro."""
    track = magda.call("tracks.create", {"name": "Rack", "type": "audio"})["id"]
    rack = magda.call("racks.create", {"trackId": track, "name": "Layers"})["id"]
    rack_path = track_path(track, steps=[{"type": "rack", "id": rack}])
    chains = [c for c in magda.call("devices.list", {"trackId": track})["chains"]
              if c["rackId"] == rack]
    second = magda.call("chains.create", {"rackPath": rack_path, "name": "FM"})["id"]
    chain_paths = [chains[0]["nodePath"],
                   track_path(track, steps=rack_path["steps"] + [{"type": "chain", "id": second}])]
    synth = magda.call("devices.add", {"catalogId": "magda_polysynth",
                                       "parentPath": chain_paths[0]})["devicePath"]
    fm = magda.call("devices.add", {"catalogId": "magda_fm",
                                    "parentPath": chain_paths[1]})["devicePath"]
    magda.call("mods.create", {"devicePath": synth, "type": "lfo", "waveform": "sine",
                               "tempoSync": True, "syncDivision": 4,
                               "parameterIndex": parameter_index(magda, synth, "cutoff"),
                               "amount": 0.5})
    magda.call("macros.link", {"devicePath": fm, "macroIndex": 0,
                               "parameterIndex": parameter_index(magda, fm, "op2 level"),
                               "amount": 0.6})
    magda.call("macros.setValue", {"devicePath": fm, "macroIndex": 0, "value": 0.5})
    midi_clip(magda, track, 8, [(n, 95, b, 1.8) for b in (0, 2, 4, 6) for n in (48, 55, 60)])


def sidechain(magda):
    """A kick ducking a pad through the Compressor's audio sidechain."""
    kick, _ = add_track(magda, "Kick", "magda_kick")
    midi_clip(magda, kick, 8, beats(8, 1, 36, 120))
    pad, (synth, compressor) = add_track(magda, "Pad", "magda_polysynth", "magda_compressor")
    midi_clip(magda, pad, 8, [(n, 100, 0, 8) for n in (57, 60, 64)])
    magda.call("sidechains.set", {"ownerPath": compressor, "sourceEndpointId": f"track:{kick}",
                                  "type": "audio", "enabled": True})


def drum_grid(magda):
    """Four Drum Grid pads, each hosting one of MAGDA's drum devices."""
    track, (grid,) = add_track(magda, "Drum Grid", "drumgrid")
    notes = {}
    for index, device in enumerate(("magda_kick", "magda_snare", "magda_hat", "magda_clap")):
        pad = magda.call("pads.create", {"gridPath": grid, "padIndex": index})
        magda.call("pads.setDevice", {"gridPath": grid, "padIndex": index, "catalogId": device})
        notes[device] = pad["midiNote"]
    pattern = (beats(8, 1, notes["magda_kick"], 115) +
               [(notes["magda_snare"], 105, b, 0.25) for b in (1, 3, 5, 7)] +
               beats(8, 0.5, notes["magda_hat"], 80) +
               [(notes["magda_clap"], 100, b, 0.25) for b in (3.5, 7.5)])
    midi_clip(magda, track, 8, pattern)


def plugin_state(magda):
    """Surge XT on a patch chosen by hand in the plugin: the API cannot select one, so the
    saved project is its own seed and only the clip is built here."""
    seed = SMOKE_DIR / "plugin-state" / "plugin-state.mgd"
    wait_for_job(magda, magda.call("project.open", {
        "path": str(seed), "dirtyPolicy": "discard", "autosavePolicy": "ignore",
        "missingMediaPolicy": "fail", "unavailableDevicePolicy": "fail"}))
    listing = magda.call("tracks.list")
    tracks = listing["items"] if isinstance(listing, dict) else listing
    track = next(t["id"] for t in tracks if t["name"] == "Surge")
    for clip in magda.call("clips.list", {"trackId": track, "view": "arrangement"})["items"]:
        magda.call("clips.delete", {"clipId": clip["id"]})
    # The patch is a bass: one note at a time, an eighth-note line.
    line = [48, 48, 55, 48, 51, 48, 55, 58] * 2
    midi_clip(magda, track, 8, [(n, 105, i * 0.5, 0.4) for i, n in enumerate(line)])


LOOP = SMOKE_DIR / "assets" / "loop-100bpm.wav"


def load_sample(magda, project, track, start=0.0):
    """The test loop, copied into the project's own media first so the save points inside it."""
    local = SMOKE_DIR / project / f"{project}_Media" / "imported" / LOOP.name
    local.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(LOOP, local)
    return magda.call("clips.loadSample", {"samplePath": str(local), "placement": {
        "view": "arrangement", "trackId": track, "startBeat": start}})["id"]


def warp(magda):
    """A 100 BPM loop stretched to 120 BPM, once through each stretch engine."""
    magda.call("project.setTempo", {"tempo": 120})
    for name, stretch in (("Signalsmith", "signalsmith"), ("SoundTouch", "soundtouch")):
        track = magda.call("tracks.create", {"name": name, "type": "audio"})["id"]
        clip = load_sample(magda, "warp", track)
        magda.call("clips.updateAudio", {"clipId": clip, "playback": "beat", "sourceBpm": 100,
                                         "stretch": stretch})


def reverse_fades(magda):
    """The loop reversed on one track, faded in and out on another, both as tape."""
    magda.call("project.setTempo", {"tempo": 100})
    reversed_track = magda.call("tracks.create", {"name": "Reversed", "type": "audio"})["id"]
    magda.call("clips.updateAudio", {"clipId": load_sample(magda, "reverse-fades", reversed_track),
                                     "reversed": True})
    faded = magda.call("tracks.create", {"name": "Faded", "type": "audio"})["id"]
    magda.call("clips.updateAudio", {"clipId": load_sample(magda, "reverse-fades", faded), "fadeInSeconds": 1.0,
                                     "fadeOutSeconds": 1.5, "fadeInCurve": "convex",
                                     "fadeOutCurve": "s_curve"})


def multi_out(magda):
    """A Drum Grid with the kick on its main mix and the snare on a bus of its own."""
    track, (grid,) = add_track(magda, "Drums", "drumgrid")
    notes = {}
    for index, device in enumerate(("magda_kick", "magda_snare")):
        pad = magda.call("pads.create", {"gridPath": grid, "padIndex": index})
        magda.call("pads.setDevice", {"gridPath": grid, "padIndex": index, "catalogId": device})
        notes[device] = pad["midiNote"]
    magda.call("pads.update", {"gridPath": grid, "padIndex": 1, "outputBus": 1})
    midi_clip(magda, track, 8, beats(8, 1, notes["magda_kick"], 115) +
              [(notes["magda_snare"], 105, b, 0.25) for b in (1, 3, 5, 7)])


PROJECTS = {
    "faust-devices": faust_devices,
    "tempo-automation": tempo_automation,
    "session-launcher": session_launcher,
    "rack-modulation": rack_modulation,
    "sidechain": sidechain,
    "drum-grid": drum_grid,
    "plugin-state": plugin_state,
    "warp": warp,
    "reverse-fades": reverse_fades,
    "multi-out": multi_out,
}


def build(magda, name, recipe, discard):
    magda.call("project.new", {"discardUnsavedChanges": discard})
    recipe(magda)
    target = SMOKE_DIR / name / f"{name}.mgd"
    job = magda.call("project.saveAs", {"path": str(target), "overwritePolicy": "replace",
                                        "mediaPolicy": "copy"})
    written = wait_for_job(magda, job)["result"]["project"]["path"]
    print(f"{name}: {written}", flush=True)


def main():
    args = sys.argv[1:]
    discard = "--discard" in args
    names = [a for a in args if a != "--discard"] or list(PROJECTS)
    unknown = [n for n in names if n not in PROJECTS]
    if unknown:
        raise SystemExit(f"unknown projects {unknown}; known: {list(PROJECTS)}")
    magda = Magda()
    for name in names:
        build(magda, name, PROJECTS[name], discard)


if __name__ == "__main__":
    main()
