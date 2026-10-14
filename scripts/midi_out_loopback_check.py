#!/usr/bin/env python3
"""Check a track's hardware MIDI Out through a MIDI loopback, with no synth plugged in (#3048).

One track plays a clip with its MIDI Out on the loopback port; a second track, armed on
that port, records what arrives. A second pass mutes the sender and expects nothing.

Manual setup, once:
  - macOS: Audio MIDI Setup > MIDI Studio > IAC Driver > "Device is online".
  - MAGDA running with MCP on, the edit and transport scopes granted to claude-code, and the
    loopback enabled as an input in Settings > MIDI.
  - A project where no other track sends to the loopback: anything else on it is recorded too.

    python3 scripts/midi_out_loopback_check.py              # the first port named IAC
    python3 scripts/midi_out_loopback_check.py --port "IAC Driver Bus 1"
"""

import argparse
import sys
import time

from build_smoke_projects import midi_clip
from build_synthstack_session import Magda

NOTES = [(60, 100, 0.0, 0.5), (64, 100, 1.0, 0.5), (67, 100, 2.0, 0.5), (72, 100, 3.0, 0.5)]


def items(result):
    return result.get("items", []) if isinstance(result, dict) else result


def endpoint(magda, port, direction):
    for found in items(magda.call("routing.endpoints.list")):
        if (found["media"] == "midi" and found["direction"] == direction and
                found["kind"] == "hardware" and found["name"] == port):
            return found["id"]
    sys.exit(f"MAGDA has no MIDI {direction} named {port}: is it enabled in Settings > MIDI?")


def find_port(magda, wanted):
    outputs = items(magda.call("midi.listOutputPorts"))
    matches = [p for p in outputs if (wanted or "IAC") in p]
    if not matches:
        sys.exit(f"no MIDI output matching {wanted or 'IAC'}; outputs are {outputs}")
    return matches[0]


def record_pass(magda, receiver, seconds):
    """Play from the top while the receiver records; the pitches of the take, or None."""
    before = {c["id"] for c in items(magda.call("clips.list", {"trackId": receiver}))}
    magda.call("tracks.update", {"trackId": receiver, "recordArmed": True, "inputMonitor": "in"})
    magda.call("transport.seek", {"positionBeats": 0})
    magda.call("transport.setRecording", {"recording": True})
    magda.call("transport.play")
    time.sleep(seconds)
    magda.call("transport.stop")
    magda.call("transport.setRecording", {"recording": False})
    magda.call("tracks.update", {"trackId": receiver, "recordArmed": False})
    time.sleep(0.5)

    clips = [c for c in items(magda.call("clips.list", {"trackId": receiver}))
             if c["id"] not in before]
    if not clips:
        return None
    events = items(magda.call("clips.listMidiEvents", {"clipId": clips[-1]["id"]}))
    return [e["note"] for e in events if e["type"] == "note"]


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--port", help="loopback port name, or part of it (default: IAC)")
    args = parser.parse_args()

    magda = Magda()
    port = find_port(magda, args.port)

    # The sender keeps its default input, so the sendback guard is exercised too: hearing
    # its own port back would loop.
    sender = magda.call("tracks.create", {"name": "MIDI Out", "type": "audio"})["id"]
    midi_clip(magda, sender, 4, NOTES)
    magda.call("routing.set", {"trackId": sender,
                               "midiOutputEndpointId": endpoint(magda, port, "output")})

    receiver = magda.call("tracks.create", {"name": "MIDI Out Take", "type": "audio"})["id"]
    magda.call("routing.set", {"trackId": receiver,
                               "midiInputEndpointId": endpoint(magda, port, "input")})

    tempo = magda.call("transport.get").get("tempo", 120.0)
    seconds = 4 * 60.0 / tempo + 1.0

    sent = [n for n, _, _, _ in NOTES]
    heard = record_pass(magda, receiver, seconds)
    print(f"sender {sender} -> receiver {receiver}: sent {sent}, recorded {heard}")
    ok = heard == sent

    magda.call("tracks.update", {"trackId": sender, "muted": True})
    muted = record_pass(magda, receiver, seconds)
    print(f"muted sender: recorded {muted or []}")
    ok = ok and not muted

    print("PASS" if ok else "FAIL: expected the clip's notes once, and nothing while muted")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
