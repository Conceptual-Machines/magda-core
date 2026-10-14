#!/usr/bin/env python3
"""Record a scripted MPE performance into a running MAGDA through a MIDI loopback (#1571).

MAGDA sends the performance out of a loopback port with midi.send and records it back in
on a new armed track, as if an MPE controller were plugged in. Injected track MIDI is not
used: it is audition input, which a take leaves out.

Manual setup, once:
  - macOS: Audio MIDI Setup > MIDI Studio > IAC Driver > "Device is online".
  - MAGDA running with MCP on, and the edit and hardware-midi scopes granted to claude-code.

    python3 scripts/mpe_record_check.py              # the first port named IAC
    python3 scripts/mpe_record_check.py --port "IAC Driver Bus 1"

The API does not return a note's pitch expression, so the glides are checked by eye in the
piano roll. What it can check is that the bends, CC74 and pressure left the clip's lanes for
the notes.
"""

import argparse
import sys
import time

from build_synthstack_session import Magda

PITCH_WHEEL = 0xE0
CONTROL_CHANGE = 0xB0
CHANNEL_PRESSURE = 0xD0
TIMBRE = 74
NOTE_ON = 0x90
NOTE_OFF = 0x80
REST = 8192
RANGE = 48.0


def wheel(semitones):
    return max(0, min(16383, round(REST + semitones / RANGE * 8191)))


def send(magda, port, *data):
    magda.call("midi.send", {"port": port, "bytes": list(data)})


def bend(magda, port, channel, semitones):
    value = wheel(semitones)
    send(magda, port, PITCH_WHEEL | (channel - 1), value & 0x7F, value >> 7)


def note(magda, port, channel, number, on):
    status = (NOTE_ON if on else NOTE_OFF) | (channel - 1)
    send(magda, port, status, number, 100 if on else 0)


def glide(magda, port, channel, to, seconds, steps=24):
    for step in range(1, steps + 1):
        bend(magda, port, channel, to * step / steps)
        time.sleep(seconds / steps)


def sweep(magda, port, status, channel, frm, to, seconds, steps=24):
    """A 7-bit dimension, timbre or pressure, moved from one value to another."""
    for step in range(steps + 1):
        value = round(frm + (to - frm) * step / steps)
        if status == CONTROL_CHANGE:
            send(magda, port, status | (channel - 1), TIMBRE, value)
        else:
            send(magda, port, status | (channel - 1), value)
        time.sleep(seconds / steps)


def perform(magda, port):
    """Three notes on three member channels, the way an MPE controller sends them."""
    bend(magda, port, 2, 0.0)
    note(magda, port, 2, 60, True)
    time.sleep(0.3)

    bend(magda, port, 3, 0.0)
    note(magda, port, 3, 64, True)
    glide(magda, port, 2, 2.0, 1.0)
    glide(magda, port, 3, -1.0, 0.6)
    note(magda, port, 2, 60, False)
    note(magda, port, 3, 64, False)
    time.sleep(0.3)

    # Bent ahead of its note: the note starts three semitones up.
    bend(magda, port, 4, 3.0)
    note(magda, port, 4, 67, True)
    time.sleep(0.8)
    note(magda, port, 4, 67, False)
    time.sleep(0.3)

    # Slide and pressure: timbre opens dark and brightens, pressure swells and fades.
    send(magda, port, CONTROL_CHANGE | 4, TIMBRE, 20)
    note(magda, port, 5, 72, True)
    sweep(magda, port, CONTROL_CHANGE, 5, 20, 120, 0.8)
    note(magda, port, 6, 76, True)
    sweep(magda, port, CHANNEL_PRESSURE, 6, 0, 127, 0.4)
    sweep(magda, port, CHANNEL_PRESSURE, 6, 127, 30, 0.4)
    note(magda, port, 5, 72, False)
    note(magda, port, 6, 76, False)

    for channel in (2, 3, 4):
        bend(magda, port, channel, 0.0)
    send(magda, port, CONTROL_CHANGE | 4, TIMBRE, 64)
    send(magda, port, CHANNEL_PRESSURE | 5, 0)


def items(result):
    return result.get("items", []) if isinstance(result, dict) else result


def find_port(magda, wanted):
    outputs = items(magda.call("midi.listOutputPorts"))
    matches = [p for p in outputs if (wanted or "IAC") in p]
    if not matches:
        sys.exit(f"no MIDI output matching {wanted or 'IAC'}; outputs are {outputs}")
    return matches[0]


def find_input(magda, port):
    for endpoint in items(magda.call("routing.endpoints.list")):
        if (endpoint["media"] == "midi" and endpoint["direction"] == "input" and
                endpoint["kind"] == "hardware" and endpoint["name"] == port):
            return endpoint["id"]
    sys.exit(f"MAGDA has no MIDI input named {port}: is it enabled in Settings > MIDI?")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--port", help="loopback port name, or part of it (default: IAC)")
    args = parser.parse_args()

    magda = Magda()
    port = find_port(magda, args.port)
    source = find_input(magda, port)

    track = magda.call("tracks.create", {"name": "MPE Take", "type": "audio"})["id"]
    magda.call("devices.add", {"trackId": track, "catalogId": "magda_polysynth"})
    magda.call("routing.set", {"trackId": track, "midiInputEndpointId": source})
    magda.call("tracks.update", {"trackId": track, "recordArmed": True, "inputMonitor": "in"})

    before = {c["id"] for c in items(magda.call("clips.list", {"trackId": track}))}

    magda.call("transport.seek", {"positionBeats": 0})
    magda.call("transport.setRecording", {"recording": True})
    magda.call("transport.play")
    time.sleep(0.5)
    perform(magda, port)
    time.sleep(0.5)
    magda.call("transport.stop")
    magda.call("transport.setRecording", {"recording": False})
    magda.call("tracks.update", {"trackId": track, "recordArmed": False})
    time.sleep(0.5)

    clips = [c for c in items(magda.call("clips.list", {"trackId": track}))
             if c["id"] not in before]
    if not clips:
        sys.exit("FAIL: the take left no clip on the track")

    events = items(magda.call("clips.listMidiEvents", {"clipId": clips[-1]["id"]}))
    notes = [e for e in events if e["type"] == "note"]
    bends = [e for e in events if e["type"] == "pitchBend"]
    timbres = [e for e in events if e["type"] == "controlChange" and e["controller"] == TIMBRE]
    pressures = [e for e in events if e["type"] == "channelPressure"]

    print(f"track {track}, clip {clips[-1]['id']}: {len(notes)} notes; clip lanes hold "
          f"{len(bends)} bends, {len(timbres)} timbre, {len(pressures)} pressure")
    ok = len(notes) == 5 and not bends and not timbres and not pressures
    print("PASS" if ok else "FAIL: expected 5 notes and empty bend, CC74 and pressure lanes")
    print("Now open the clip, switch the piano roll to pitch expression, and look for:\n"
          "  C4  rising 2 semitones, E4  falling 1, G4  starting 3 up and flat.\n"
          "In the drawer's MPE mode, select C5 for its slide and E5 for its pressure.")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
