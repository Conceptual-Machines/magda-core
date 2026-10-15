#!/usr/bin/env python3
"""Check Capture Recent Audio against a running MAGDA (#2311).

Plays a Poly Synth clip, stops, and captures the last two bars: the new clip has to sit
where those bars were heard and its file has to hold sound. Then, with the transport
stopped, plays a Poly Synth live through a MIDI loopback and captures again: the phrase
has to come back as its notes, on a new track, at the playhead. Loop passes are covered by tests/engine/test_capture_ring.cpp;
the API cannot set a loop range.

Manual setup, once: MAGDA running with MCP on and the edit, transport and hardware-midi
scopes granted to claude-code, the capture buffer on, in a project with nothing else playing,
and an IAC port enabled as a MIDI input.

    python3 scripts/capture_recent_check.py
"""

import sys
import time
import wave

from build_smoke_projects import add_track, midi_clip
from build_synthstack_session import Magda
from midi_out_loopback_check import endpoint, find_port

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

    # Played live with the transport stopped: the case KEEP exists for.
    port = find_port(magda, None)
    live, _ = add_track(magda, "Live Synth", "magda_polysynth")
    magda.call("routing.set", {"trackId": live,
                               "midiInputEndpointId": endpoint(magda, port, "input")})
    magda.call("tracks.update", {"trackId": live, "inputMonitor": "in"})
    magda.call("tracks.update", {"trackId": track, "muted": True})
    magda.call("transport.seek", {"positionBeats": 8})
    time.sleep(4.0)  # longer than a phrase gap, so the clip part is not part of it
    for note in (60, 64, 67, 72):
        magda.call("midi.send", {"port": port, "bytes": [0x90, note, 110]})
        time.sleep(0.4)
        magda.call("midi.send", {"port": port, "bytes": [0x80, note, 0]})
    time.sleep(0.5)

    # Played on a MIDI device, so the notes come back on a new track named after it.
    phrase = capture(magda, 8)
    seconds = phrase["lengthBeats"] * beat
    events = magda.call("clips.listMidiEvents", {"clipId": phrase["midiClipId"]})
    notes = [e["note"] for e in (events.get("items", []) if isinstance(events, dict) else events)
             if e["type"] == "note"]
    print(f"played while stopped: new track {phrase['midiTrackId']} (live is {live}), "
          f"start {phrase['startBeat']:.2f}, {seconds:.2f} s, notes {notes}")
    ok = ok and phrase["midiTrackId"] not in (live, track) and notes == [60, 64, 67, 72]
    ok = ok and abs(phrase["startBeat"] - 8.0) < 1e-6 and 1.5 <= seconds <= 3.5

    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
