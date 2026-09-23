#!/usr/bin/env python3
"""Build the parity bench's synthstack project in a running MAGDA, over its MCP endpoint.

Sixteen tracks of MIDI into hosted synths with one or two hosted effects each, on the
plugins' default patches. Start MAGDA with MCP on and an empty project open, run this,
then save the project over tests/corpus/parity/synthstack.mgd.

    python3 scripts/build_synthstack_session.py
"""

import json
import pathlib
import random
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent / "tools/transport_check"))
from clients import McpHttpClient  # noqa: E402
from discovery import data_dir, find_records  # noqa: E402

# The client name MAGDA has granted the edit scope to.
CLIENT = "claude-code"
TEMPO = 128
BARS = 32
CLIP_BEATS = 32
PROGRESSION = [(57, "m"), (53, ""), (48, ""), (55, "")]  # Am F C G, a bar each

TRACKS = [
    ("Drums", "Addictive Drums 2", ["Pro-C 2", "Pro-L 2"], "drums"),
    ("Kick", "Kick 3", ["Pro-Q 4", "Cyberdrive"], "kick"),
    ("Bass", "Serum 2", ["Pro-Q 4", "Pro-C 2"], "bass"),
    ("Sub", "Diva", ["Pro-Q 3"], "sub"),
    ("Lead", "Serum 2", ["ValhallaDelay", "ValhallaRoom"], "lead"),
    ("Pad", "Diva", ["ShaperBox 3", "ValhallaVintageVerb"], "pad"),
    ("Chords", "Jup-8 V4", ["Pro-Q 4", "ValhallaPlate"], "chords"),
    ("Arp", "Prophet-5 V", ["ValhallaFreqEcho"], "arp"),
    ("Brass", "CS-80 V4", ["Pro-R 2"], "pad"),
    ("Keys", "Dexed", ["ValhallaSupermassive"], "chords"),
    ("Pluck", "Phase Plant", ["kHs Chorus", "Pro-Q 3"], "arp32"),
    ("Stab", "Serum 2", ["Serum 2 FX", "Pro-MB"], "stab"),
    ("Mono", "Mini V3", ["Cyberdrive"], "bass"),
    ("Texture", "Diva", ["ShaperBox 3", "ValhallaShimmer"], "pad"),
    ("Gate", "Serum 2", ["kHs Trance Gate", "ValhallaSpaceModulator"], "pad"),
    ("Sweep", "Prophet-VS V", ["ValhallaUberMod"], "lead"),
]
MASTER_FX = ["Pro-Q 4", "Pro-L 2"]
MASTER_PATH = {"trackId": -2, "section": "fx", "trackLevel": False,
               "topLevelDeviceId": None, "steps": []}


class Magda:
    def __init__(self):
        record = next(r for r in find_records(data_dir()) if r.has_mcp)
        origin, path = record.mcp_origin_and_path()
        self.client = McpHttpClient(origin, path, record.mcp_token, client_name=CLIENT, timeout=180)
        self.next_id = 0

    def call(self, name, args=None):
        self.next_id += 1
        reply = self.client.modern("tools/call", {"name": name, "arguments": args or {}},
                                   request_id=self.next_id)
        body = reply.body
        if "data:" in body[:20]:
            body = [line[5:] for line in body.splitlines() if line.startswith("data:")][-1]
        message = json.loads(body)
        if "error" in message:
            raise SystemExit(f"{name} {args}: {message['error']}")
        result = message["result"]
        if result.get("isError"):
            raise SystemExit(f"{name} {args}: {result['content']}")
        return result["structuredContent"]


def bar_notes(kind, bar, rng):
    """(note, velocity, beat in bar, length) for one bar of a track's part."""
    root, quality = PROGRESSION[bar % len(PROGRESSION)]
    triad = [root, root + (3 if quality == "m" else 4), root + 7]
    if kind == "drums":
        notes = [(36, 110, b, 0.25) for b in range(4)]
        notes += [(38, 105, b, 0.25) for b in (1, 3)]
        notes += [(42, 70 + 20 * (h % 2), h * 0.25, 0.125) for h in range(16)]
        return notes
    if kind == "kick":
        return [(36, 120, b, 0.5) for b in range(4)]
    if kind == "bass":
        return [(root - 24 + (12 if e % 4 == 3 else 0), 100, e * 0.5, 0.45) for e in range(8)]
    if kind == "sub":
        return [(root - 36, 100, b, 1.8) for b in (0, 2)]
    if kind == "lead":
        return [(rng.choice(triad) + rng.choice((12, 24)), 90, e * 0.5, 0.45)
                for e in range(8) if rng.random() < 0.7]
    if kind == "pad":
        return [(n, 80, 0, 3.95) for n in triad + [root + 12]]
    if kind == "chords":
        return [(n + 12, 90, b, 0.9) for b in (0, 1.5, 2.5) for n in triad]
    if kind in ("arp", "arp32"):
        step = 0.25 if kind == "arp" else 0.125
        cycle = triad + [n + 12 for n in triad]
        return [(cycle[e % len(cycle)] + 12, 85, e * step, step * 0.8) for e in range(int(4 / step))]
    if kind == "stab":
        return [(n, 100, b, 0.25) for b in (0.5, 1.75, 3.0) for n in triad]
    raise ValueError(kind)


def main():
    magda = Magda()
    rng = random.Random(2790)
    catalog = {}
    for device in magda.call("devices.catalog")["items"]:
        if device["format"] == "vst3":
            catalog.setdefault(device["name"], device["catalogId"])

    magda.call("project.setTempo", {"tempo": TEMPO})
    for name, instrument, effects, kind in TRACKS:
        track = magda.call("tracks.create", {"name": name, "type": "audio"})["id"]
        for device in [instrument] + effects:
            magda.call("devices.add", {"trackId": track, "catalogId": catalog[device]})
        for start in range(0, BARS * 4, CLIP_BEATS):
            clip = magda.call("clips.createMidi", {"trackId": track, "startBeat": start,
                                                   "lengthBeats": CLIP_BEATS,
                                                   "view": "arrangement"})["id"]
            for bar in range(CLIP_BEATS // 4):
                for note, velocity, beat, length in bar_notes(kind, start // 4 + bar, rng):
                    magda.call("clips.addMidiNote", {"clipId": clip, "note": note,
                                                     "velocity": velocity,
                                                     "startBeat": bar * 4 + beat,
                                                     "lengthBeats": length})
        print(name, instrument, effects, flush=True)

    for device in MASTER_FX:
        magda.call("devices.add", {"catalogId": catalog[device], "parentPath": MASTER_PATH})
    print("Master", MASTER_FX)


if __name__ == "__main__":
    main()
