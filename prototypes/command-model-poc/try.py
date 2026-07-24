"""Kick the tyres: type a request, see the DSL the command model produces.

Usage:
  python try.py                      # run the built-in probe set
  python try.py "create a bass track with serum"   # one-off
  python try.py -                    # interactive: read requests from stdin

Uses the committed checkpoint in model/artifacts/. The probe set mixes
in-distribution phrasings with novel names/wordings to see if it generalizes
past the training templates (the whole point of unk-aug).
"""
import sys

from model.intent_slot_parser import build_parser

PROBES = [
    # bread-and-butter
    "create a bass track",
    "add a new drum track called Punchy",
    "create a bass track with @serum",
    "add @ott to the vocals",
    "set the master volume to -6 dB",
    "pan the guitar hard left",
    "solo the drums",
    "make the lead track red",
    "group tracks 1, 2 and 3",
    "delete the reverb track",
    # novel names / phrasings
    "spin up a fresh synth lane named Wubby",
    "throw @saturn_2 on the kick",
    "crank the snare to +2 dB",
    "select every clip longer than 4 bars on the pads",
    # new command coverage
    "transpose the notes on the bass up 5 semitones",
    "quantize the drum notes to 16ths",
    "set the note velocity on the lead to 100",
    "delete the notes on the pads",
    "add a 4 bar clip to the keys track",
    "move the vocals to position 2",
    "select all C4 notes on the bass",
    "set the groove to Basic 8th Swing with strength 0.5",
    "what grooves are available",
    # deliberately awkward / ambiguous
    "do something cool to the mix",
    "louder",
]


def show(parse, text):
    dsl = parse(text) or "(no output)"
    print(f"  {text}")
    for line in dsl.splitlines() or [""]:
        print(f"      -> {line}")
    print()


def main():
    parse = build_parser()
    args = sys.argv[1:]
    if args == ["-"]:
        print("enter a request per line (Ctrl-D to quit):")
        for line in sys.stdin:
            if line.strip():
                show(parse, line.strip())
    elif args:
        show(parse, " ".join(args))
    else:
        print("=== command-model probe set (committed baseline checkpoint) ===\n")
        for p in PROBES:
            show(parse, p)


if __name__ == "__main__":
    main()
