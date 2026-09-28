#!/usr/bin/env python3
"""Write the parity bench's Retrospect scaling projects from the retrovid fixture.

Each is N copies of one track: a tone clip at its file's own rate into one hosted
Retrospect, in Arrangement mode. The bench runs them to see how each engine's cost
grows per hosted instance.

    python3 scripts/make_retrospect_scale.py
"""

import copy
import json
import pathlib
import zlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
SOURCE = ROOT / "tests/corpus/legacy/projects/0.13.0-retrovid.mgd"
OUT = ROOT / "tests/corpus/parity"
COUNTS = (1, 2, 4, 8, 16, 32)
TRACK_ID = 2
CLIP_ID = 11


def main():
    project = json.loads(zlib.decompress(SOURCE.read_bytes()))
    track = next(t for t in project["tracks"] if t["id"] == TRACK_ID)
    clip = next(c for c in project["clips"] if c["id"] == CLIP_ID)

    track = copy.deepcopy(track)
    track["playbackMode"] = 0
    track.pop("activeSessionClipId", None)

    OUT.mkdir(parents=True, exist_ok=True)
    for count in COUNTS:
        scaled = copy.deepcopy(project)
        scaled["project"]["name"] = "retrospect-scale-%d" % count
        scaled["tracks"] = []
        scaled["clips"] = []
        for index in range(count):
            copied = copy.deepcopy(track)
            copied["id"] = index + 1
            copied["name"] = "Retrospect %d" % (index + 1)
            copied["chainElements"][0]["device"]["id"] = index + 1
            scaled["tracks"].append(copied)

            placed = copy.deepcopy(clip)
            placed["id"] = index + 1
            placed["trackId"] = index + 1
            scaled["clips"].append(placed)

        path = OUT / ("retrospect-scale-%d.mgd" % count)
        path.write_bytes(zlib.compress(json.dumps(scaled).encode("utf-8")))
        print(path.relative_to(ROOT))


if __name__ == "__main__":
    main()
