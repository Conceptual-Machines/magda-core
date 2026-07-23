"""Key-relative chord representation and key inference.

Core conversions:
  (root pitch-class, quality-class, key tonic) <-> token "<interval>:<quality>"

The token is transposition-invariant: interval = (root - tonic) mod 12. A
deterministic inverse maps a token back to an absolute root pitch-class in a
given key, which the C++ engine then voices into MIDI.

Also provides Krumhansl-Schmuckler key finding for corpora that don't annotate
a key, and a roman-numeral display purely for human-readable eval output.
"""
from __future__ import annotations

from .vocab import QUALITY_SET, QUALITY_INTERVALS, NC

NOTE_TO_PC = {
    "C": 0, "C#": 1, "DB": 1, "D": 2, "D#": 3, "EB": 3, "E": 4, "FB": 4,
    "E#": 5, "F": 5, "F#": 6, "GB": 6, "G": 7, "G#": 8, "AB": 8, "A": 9,
    "A#": 10, "BB": 10, "B": 11, "CB": 11, "B#": 0,
}


def note_to_pc(name: str) -> int | None:
    """Parse a note-name (C, F#, Bb, ...) to a pitch class 0..11."""
    n = name.strip().upper().replace("♭", "B").replace("♯", "#")
    return NOTE_TO_PC.get(n)


def chord_to_token(root_pc: int, quality: str, tonic_pc: int) -> str:
    """(root pitch-class, quality-class, tonic) -> key-relative token."""
    interval = (root_pc - tonic_pc) % 12
    return f"{interval}:{quality}"


def token_to_root(token: str, tonic_pc: int) -> tuple[int, str] | None:
    """Inverse: token + key tonic -> (absolute root pitch-class, quality)."""
    if token == NC or ":" not in token:
        return None
    ivl, qual = token.split(":", 1)
    try:
        interval = int(ivl)
    except ValueError:
        return None
    if qual not in QUALITY_SET:
        return None
    return (tonic_pc + interval) % 12, qual


# Diatonic interval -> roman degree label (major-key reference); other
# intervals are shown chromatically. Purely for readable eval dumps.
_ROMAN = {0: "I", 2: "II", 4: "III", 5: "IV", 7: "V", 9: "VI", 11: "VII"}


def roman_display(token: str) -> str:
    if token == NC:
        return "N.C."
    if ":" not in token:      # special tokens (<bos>/<eos>/<pad>/<unk>)
        return token
    parsed = token.split(":", 1)
    interval, qual = int(parsed[0]), parsed[1]
    base = _ROMAN.get(interval, f"[{interval}]")
    if qual in ("min", "min7", "min6", "dim", "dim7", "hdim7"):
        base = base.lower()
    suffix = {"maj": "", "min": "", "dom7": "7", "maj7": "maj7", "min7": "7",
              "dim": "°", "dim7": "°7", "hdim7": "ø7",
              "aug": "+", "sus4": "sus4", "sus2": "sus2",
              "maj6": "6", "min6": "6"}.get(qual, qual)
    return base + suffix


# Krumhansl-Kessler key profiles (major, minor), normalized at use.
_KK_MAJOR = [6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88]
_KK_MINOR = [6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17]


def pc_histogram_from_chords(chords) -> list[float]:
    """Weighted 12-bin pitch-class histogram from [(root_pc, quality)] using
    each chord's actual tones (root emphasized). Far better for key inference
    than counting roots alone."""
    hist = [0.0] * 12
    for root_pc, qual in chords:
        for iv in QUALITY_INTERVALS.get(qual, (0,)):
            hist[(root_pc + iv) % 12] += 1.0
        hist[root_pc % 12] += 0.5   # extra weight on the root
    return hist


def infer_key(pc_histogram: list[float]) -> tuple[int, str]:
    """Krumhansl-Schmuckler: best (tonic pitch-class, mode) for a 12-bin
    pitch-class histogram. mode in {"maj","min"}."""
    def corr(profile, hist, shift):
        rot = [profile[(i - shift) % 12] for i in range(12)]
        n = 12
        mp, mh = sum(rot) / n, sum(hist) / n
        num = sum((rot[i] - mp) * (hist[i] - mh) for i in range(12))
        dp = sum((rot[i] - mp) ** 2 for i in range(12)) ** 0.5
        dh = sum((hist[i] - mh) ** 2 for i in range(12)) ** 0.5
        return num / (dp * dh) if dp and dh else -1.0

    best, best_key = -2.0, (0, "maj")
    for tonic in range(12):
        for profile, mode in ((_KK_MAJOR, "maj"), (_KK_MINOR, "min")):
            c = corr(profile, pc_histogram, tonic)
            if c > best:
                best, best_key = c, (tonic, mode)
    return best_key
