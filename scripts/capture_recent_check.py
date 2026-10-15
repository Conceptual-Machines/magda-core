#!/usr/bin/env python3
"""Check Capture Recent Audio against a running MAGDA (#2311).

Plays a Poly Synth clip, stops, and captures the last two bars: the new clip has to sit
where those bars were heard and its file has to hold sound. Loop passes are covered by
tests/engine/test_capture_ring.cpp; the API cannot set a loop range.

Manual setup, once: MAGDA running with MCP on and the edit and transport scopes granted to
claude-code, in a project with nothing else playing.

    python3 scripts/capture_recent_check.py
"""

import sys
import time
import wave

from build_smoke_projects import add_track, midi_clip
from build_synthstack_session import Magda

NOTES = [(48 + (i % 4) * 5, 110, float(i), 0.9) for i in range(16)]


def peak(path):
    """The loudest sample of a 24-bit WAV, as a fraction of full scale."""
    with wave.open(path, "rb") as audio:
        frames = audio.readframes(audio.getnframes())
        width = audio.getsampwidth()
    loudest = 0
    for i in range(0, len(frames) - width + 1, width * 64):
        loudest = max(loudest, abs(int.from_bytes(frames[i:i + width], "little", signed=True)))
    return loudest / float(1 << (8 * width - 1))


def capture(magda, bars):
    return magda.call("capture.recentMaster", {"bars": bars})


def main():
    magda = Magda()
    track, _ = add_track(magda, "Capture Source", "magda_polysynth")
    midi_clip(magda, track, 16, NOTES)
    tempo = magda.call("transport.get").get("tempo", 120.0)
    beat = 60.0 / tempo

    magda.call("transport.setLoopEnabled", {"enabled": False})
    magda.call("transport.seek", {"positionBeats": 0})
    magda.call("transport.play")
    time.sleep(10.4 * beat)
    magda.call("transport.stop")
    time.sleep(0.3)

    result = capture(magda, 2)
    loudest = peak(result["filePath"])
    print(f"two bars: start {result['startBeat']:.2f}, length {result['lengthBeats']:.2f}, "
          f"peak {loudest:.3f}")
    ok = abs(result["startBeat"] - 4.0) < 1e-6 and 6.0 <= result["lengthBeats"] <= 7.0
    ok = ok and loudest > 0.01

    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
