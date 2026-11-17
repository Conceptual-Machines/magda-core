#!/usr/bin/env python3
"""Build a heavy instrument project in a running MAGDA, over its MCP endpoint, to measure the
audio callback under many instruments (#1898).

MAGDA's own devices only: Drum Grids whose pads are Samplers playing generated tones, Poly Synths
at eight unison voices, FM and struck voices, each with effects, in group buses with two returns
and a master chain. The project and its samples are saved under local/load/<name>/.
Start MAGDA with MCP on and the edit scope granted to claude-code, then:

    python3 scripts/build_instrument_load_session.py
    python3 scripts/build_instrument_load_session.py --name instrument-load-flat --flat --frozen-copy

--scale is how many copies of the instrument set to build (12 tracks each, default 2).
--flat leaves out the group buses. --frozen-copy, which needs --flat, also saves <name>-frozen
with every instrument frozen to audio: the same parts played as audio files. --kontakt-preset <name or id> adds --kontakt-tracks tracks (default 8)
made from a track preset saved by hand with a library loaded in Kontakt, which the API cannot
load. --discard drops unsaved changes in the open project.
"""

import argparse
import math
import pathlib
import random
import struct
import time
import wave

from build_smoke_projects import add_notes, add_track, parameter_index, wait_for_job
from build_synthstack_session import MASTER_PATH, Magda, bar_notes

OUT_DIR = pathlib.Path(__file__).resolve().parent.parent / "local" / "load"
TEMPO = 128
BARS = 32
CLIP_BEATS = 32
RATE = 48000
PADS = 16
TONE_SECONDS = 4.0
SEND_LEVEL = 0.3

# (name, pad pattern, effects): a Drum Grid of PADS Samplers.
SAMPLER_TRACKS = [
    ("Keys", "keys", ["magda_eq", "magda_compressor", "magda_chorus"]),
    ("Kit", "kit", ["magda_eq", "magda_compressor", "magda_saturator"]),
    ("Texture", "texture", ["magda_eq", "magda_phaser", "magda_delay"]),
    ("Plucks", "plucks", ["magda_eq", "magda_compressor", "magda_flanger"]),
]
# (name, instrument, bar_notes part, effects).
SYNTH_TRACKS = [
    ("Pad", "magda_polysynth", "pad", ["magda_chorus", "magda_eq"]),
    ("Chords", "magda_polysynth", "chords", ["magda_eq", "magda_compressor"]),
    ("Arp", "magda_polysynth", "arp", ["magda_delay", "magda_eq"]),
    ("Lead", "magda_polysynth", "lead", ["magda_saturator", "magda_delay"]),
    ("Bass", "magda_fm", "bass", ["magda_compressor", "magda_eq"]),
    ("Stab", "magda_fm", "stab", ["magda_phaser", "magda_eq"]),
    ("Mallets", "magda_marimba", "arp32", ["magda_eq"]),
    ("Bells", "magda_bell", "chords", ["magda_chorus", "magda_eq"]),
]
GROUP_FX = ["magda_compressor", "magda_eq"]
RETURNS = [("Reverb", "magda_reverb"), ("Echo", "magda_delay")]
MASTER_FX = ["magda_eq", "magda_multiband", "magda_limiter"]
KONTAKT_PARTS = ["chords", "pad", "arp", "bass"]
# Poly Synth's "Unison Voices" menu: 0..3 are 1, 2, 4 and 8 voices.
EIGHT_VOICES = 3


def write_tone(path, frequency):
    """A stereo tone of a few decaying partials, the right channel slightly detuned."""
    data = bytearray()
    for i in range(int(TONE_SECONDS * RATE)):
        t = i / RATE
        level = 0.2 * math.exp(-0.7 * t) * 32767
        left = sum(math.sin(2 * math.pi * frequency * k * t) / k for k in (1, 2, 3, 5))
        right = sum(math.sin(2 * math.pi * frequency * 1.003 * k * t) / k for k in (1, 2, 3, 5))
        data += struct.pack("<hh", int(level * left), int(level * right))
    with wave.open(str(path), "wb") as out:
        out.setnchannels(2)
        out.setsampwidth(2)
        out.setframerate(RATE)
        out.writeframes(bytes(data))


def tones(name):
    """PADS tones a semitone apart from A2, in the project's own media folder."""
    folder = OUT_DIR / name / f"{name}_Media" / "imported"
    folder.mkdir(parents=True, exist_ok=True)
    paths = [folder / f"tone-{i:02d}.wav" for i in range(PADS)]
    for i, path in enumerate(paths):
        if not path.exists():
            write_tone(path, 110 * 2 ** (i / 12))
    return paths


def pad_part(kind, bar, notes, rng):
    """(note, velocity, beat in bar, length) for one bar on a grid of Sampler pads."""
    if kind == "keys":
        return [(notes[(bar * 3 + i + c) % PADS], 90, i * 0.5, 2.0)
                for i in range(8) for c in (0, 4, 7)]
    if kind == "kit":
        return [(notes[s % PADS], 70 + 10 * (s % 4), s * 0.25, 0.25) for s in range(16)]
    if kind == "texture":
        return [(notes[(bar + i * 3) % PADS], 75, b, 4.0) for b in range(4) for i in range(4)]
    if kind == "plucks":
        return [(notes[rng.randrange(PADS)], 85, e * 0.125, 0.5)
                for e in range(32) if rng.random() < 0.6]
    raise ValueError(kind)


def fill_clips(magda, track, part):
    """Arrangement clips across the song, bar by bar from part(bar), cut at each clip's end."""
    for start in range(0, BARS * 4, CLIP_BEATS):
        clip = magda.call("clips.createMidi", {"lengthBeats": CLIP_BEATS, "placement": {
            "view": "arrangement", "trackId": track, "startBeat": start}})["id"]
        notes = []
        for bar in range(CLIP_BEATS // 4):
            for note, velocity, beat, length in part(start // 4 + bar):
                at = bar * 4 + beat
                notes.append((note, velocity, at, min(length, CLIP_BEATS - at)))
        add_notes(magda, clip, notes)


def sampler_track(magda, name, kind, effects, samples, rng):
    track, (grid, *_) = add_track(magda, name, "drumgrid", *effects)
    notes = []
    for index, sample in enumerate(samples):
        notes.append(magda.call("pads.create", {"gridPath": grid, "padIndex": index})["midiNote"])
        magda.call("pads.setSample", {"gridPath": grid, "padIndex": index,
                                      "samplePath": str(sample)})
    fill_clips(magda, track, lambda bar: pad_part(kind, bar, notes, rng))
    return track


def synth_track(magda, name, instrument, part, effects, rng):
    track, (synth, *_) = add_track(magda, name, instrument, *effects)
    if instrument == "magda_polysynth":
        magda.call("devices.setParameter", {
            "devicePath": synth, "parameterIndex": parameter_index(magda, synth, "unison voices"),
            "value": EIGHT_VOICES})
    fill_clips(magda, track, lambda bar: bar_notes(part, bar, rng))
    return track


def kontakt_tracks(magda, preset, count, rng):
    listing = magda.call("trackPresets.list")
    items = listing["items"] if isinstance(listing, dict) else listing
    match = next((p for p in items if preset in (p["id"], p["name"])), None)
    if match is None:
        raise SystemExit(f"no track preset {preset!r}; saved: {[p['name'] for p in items]}")
    tracks = []
    for i in range(count):
        track = magda.call("tracks.createFromPreset", {"presetId": match["id"]})["trackId"]
        magda.call("tracks.update", {"trackId": track, "name": f"Kontakt {i + 1}"})
        part = KONTAKT_PARTS[i % len(KONTAKT_PARTS)]
        fill_clips(magda, track, lambda bar, part=part: bar_notes(part, bar, rng))
        tracks.append(track)
    return tracks


def check_catalog(magda):
    available = {d["catalogId"] for d in magda.call("devices.catalog")["items"]}
    needed = {"drumgrid", *GROUP_FX, *MASTER_FX, *(d for _, d in RETURNS)}
    needed |= {d for _, _, fx in SAMPLER_TRACKS for d in fx}
    needed |= {d for _, i, _, fx in SYNTH_TRACKS for d in [i] + fx}
    missing = sorted(needed - available)
    if missing:
        raise SystemExit(f"not in MAGDA's device catalog: {missing}")


def save(magda, name, retries=1):
    target = OUT_DIR / name / f"{name}.mgd"
    job = magda.call("project.saveAs", {"path": str(target), "overwritePolicy": "replace",
                                        "mediaPolicy": "copy"})
    try:
        print(f"saved {wait_for_job(magda, job)['result']['project']['path']}", flush=True)
    except SystemExit as error:
        # Right after a freeze the project can still change under a save, which then conflicts.
        if retries == 0 or "revision changed" not in str(error):
            raise
        time.sleep(2)
        save(magda, name, retries - 1)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--name", default="instrument-load")
    parser.add_argument("--scale", type=int, default=2)
    parser.add_argument("--flat", action="store_true")
    parser.add_argument("--frozen-copy", action="store_true")
    parser.add_argument("--kontakt-preset")
    parser.add_argument("--kontakt-tracks", type=int, default=8)
    parser.add_argument("--discard", action="store_true")
    args = parser.parse_args()
    if args.frozen_copy and not args.flat:
        parser.error("--frozen-copy needs --flat: MAGDA freezes only media tracks feeding no group")

    magda = Magda()
    check_catalog(magda)
    rng = random.Random(1898)
    samples = tones(args.name)
    magda.call("project.new", {"discardUnsavedChanges": args.discard})
    magda.call("project.setTempo", {"tempo": TEMPO})

    groups = {"Samplers": [], "Synths": [], "Kontakt": []}
    for copy in range(1, args.scale + 1):
        for name, kind, effects in SAMPLER_TRACKS:
            groups["Samplers"].append(
                sampler_track(magda, f"{name} {copy}", kind, effects, samples, rng))
        for name, instrument, part, effects in SYNTH_TRACKS:
            groups["Synths"].append(
                synth_track(magda, f"{name} {copy}", instrument, part, effects, rng))
        print(f"instrument set {copy} of {args.scale}", flush=True)
    if args.kontakt_preset:
        groups["Kontakt"] = kontakt_tracks(magda, args.kontakt_preset, args.kontakt_tracks, rng)

    instruments = [t for members in groups.values() for t in members]
    returns = []
    for name, effect in RETURNS:
        aux = magda.call("tracks.create", {"name": name, "type": "aux"})["id"]
        magda.call("devices.add", {"trackId": aux, "catalogId": effect})
        returns.append(aux)
    for track in instruments:
        for aux in returns:
            magda.call("sends.create", {"trackId": track, "destinationEndpointId": f"track:{aux}",
                                        "level": SEND_LEVEL})
    for name, members in groups.items():
        if members and not args.flat:
            group = magda.call("tracks.group", {"trackIds": members, "name": name})["id"]
            for effect in GROUP_FX:
                magda.call("devices.add", {"trackId": group, "catalogId": effect})
    for effect in MASTER_FX:
        magda.call("devices.add", {"catalogId": effect, "parentPath": MASTER_PATH})
    save(magda, args.name)

    if args.frozen_copy:
        for track in instruments:
            wait_for_job(magda, magda.call("tracks.freeze", {"trackId": track}))
        save(magda, f"{args.name}-frozen")


if __name__ == "__main__":
    main()
