#!/usr/bin/env python3
"""Check several tracks driving one track on their own MIDI channels, through a loopback (#2209).

Two part tracks send their MIDI Out to one instrument track, on channels 2 and 3. That
track's MIDI Out is the loopback port, and a recorder track armed on the port records it
back, filtered by its input channel: channel 2 hears only the first part, channel 3 only the
second, any channel both.

Manual setup, once:
  - macOS: Audio MIDI Setup > MIDI Studio > IAC Driver > "Device is online".
  - MAGDA running with MCP on, the edit and transport scopes granted to claude-code, and the
    loopback enabled as an input in Settings > MIDI.
  - A project where no other track sends to the loopback: anything else on it is recorded too.

    python3 scripts/midi_channel_loopback_check.py
    python3 scripts/midi_channel_loopback_check.py --port "IAC Driver Bus 1"
"""

import argparse
import sys

from build_smoke_projects import midi_clip
from build_synthstack_session import Magda
from midi_out_loopback_check import endpoint, find_port, items, record_pass

FIRST = [(60, 100, 0.0, 0.5), (64, 100, 2.0, 0.5)]
SECOND = [(67, 100, 1.0, 0.5), (72, 100, 3.0, 0.5)]


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--port", help="loopback port name, or part of it (default: IAC)")
    args = parser.parse_args()

    magda = Magda()
    port = find_port(magda, args.port)

    instrument = magda.call("tracks.create", {"name": "Multitimbral", "type": "audio"})["id"]
    magda.call("routing.set", {"trackId": instrument,
                               "midiOutputEndpointId": endpoint(magda, port, "output")})

    for name, notes, channel in (("Part 2", FIRST, 2), ("Part 3", SECOND, 3)):
        part = magda.call("tracks.create", {"name": name, "type": "audio"})["id"]
        midi_clip(magda, part, 4, notes)
        magda.call("routing.set", {"trackId": part, "midiOutputEndpointId": f"track:{instrument}",
                                   "midiOutputChannel": channel})

    recorder = magda.call("tracks.create", {"name": "Channel Take", "type": "audio"})["id"]
    magda.call("routing.set", {"trackId": recorder,
                               "midiInputEndpointId": endpoint(magda, port, "input")})

    tempo = magda.call("transport.get").get("tempo", 120.0)
    seconds = 4 * 60.0 / tempo + 1.0

    expected = {2: [n for n, *_ in FIRST], 3: [n for n, *_ in SECOND],
                0: sorted(n for n, *_ in FIRST + SECOND)}
    ok = True
    for channel in (2, 3, 0):
        magda.call("routing.set", {"trackId": recorder, "midiInputChannel": channel})
        heard = sorted(record_pass(magda, recorder, seconds) or [])
        print(f"recorder on channel {channel or 'any'}: recorded {heard}, "
              f"expected {expected[channel]}")
        ok = ok and heard == expected[channel]

    routing = magda.call("routing.get", {"trackId": recorder})
    print(f"recorder routing: input channel {routing['midiInputChannel']}")

    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
