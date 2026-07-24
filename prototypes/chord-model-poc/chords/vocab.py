"""Quality-class vocabulary and chord-symbol parsing.

MAGDA's ChordQuality enum has 36 values; we model a reduced set of
quality-CLASSES so the token vocab stays tiny. The class -> ChordQuality
mapping is applied at the C++ integration boundary (Phase 2), not here.

A token is a string "<interval>:<quality>", where interval is 0..11 semitones
from the key tonic and quality is one of QUALITY_CLASSES. Special tokens cover
sequence boundaries, padding, unknowns, and "no chord".
"""
from __future__ import annotations

# Canonical reduced qualities. Order is stable (used for indexing).
QUALITY_CLASSES = [
    "maj", "min", "dom7", "maj7", "min7",
    "dim", "dim7", "hdim7", "aug", "sus4", "sus2", "maj6", "min6",
]
QUALITY_SET = set(QUALITY_CLASSES)

# Semitone offsets per quality-class (mirrors ChordUtils::getChordIntervals on
# the C++ side). Used for key inference from chord tones.
QUALITY_INTERVALS = {
    "maj": (0, 4, 7), "min": (0, 3, 7), "dom7": (0, 4, 7, 10),
    "maj7": (0, 4, 7, 11), "min7": (0, 3, 7, 10), "dim": (0, 3, 6),
    "dim7": (0, 3, 6, 9), "hdim7": (0, 3, 6, 10), "aug": (0, 4, 8),
    "sus4": (0, 5, 7), "sus2": (0, 2, 7), "maj6": (0, 4, 7, 9),
    "min6": (0, 3, 7, 9),
}

# Special tokens.
PAD, BOS, EOS, UNK, NC = "<pad>", "<bos>", "<eos>", "<unk>", "<nc>"
SPECIALS = [PAD, BOS, EOS, UNK, NC]

# Map many surface spellings (Harte shorthand, jazz, ABC) -> quality class.
# Extensions beyond the 7th collapse to their base; inversions/adds are
# stripped before lookup (see normalize_quality).
_QUALITY_ALIASES = {
    "": "maj", "maj": "maj", "major": "maj", "M": "maj",
    "min": "min", "minor": "min", "m": "min", "-": "min",
    "7": "dom7", "dom": "dom7", "dom7": "dom7", "9": "dom7", "11": "dom7", "13": "dom7",
    "maj7": "maj7", "M7": "maj7", "maj9": "maj7", "maj13": "maj7",
    "min7": "min7", "m7": "min7", "-7": "min7", "min9": "min7", "min11": "min7", "m9": "min7",
    "dim": "dim", "o": "dim",
    "dim7": "dim7", "o7": "dim7",
    "hdim7": "hdim7", "min7b5": "hdim7", "m7b5": "hdim7", "ø": "hdim7", "ø7": "hdim7",
    "aug": "aug", "+": "aug", "aug7": "aug",
    "sus4": "sus4", "sus": "sus4", "7sus4": "sus4", "9sus4": "sus4",
    "sus2": "sus2",
    "maj6": "maj6", "6": "maj6",
    "min6": "min6", "m6": "min6", "-6": "min6",
}


def normalize_quality(raw: str) -> str | None:
    """Map a surface quality string to a QUALITY_CLASS, or None if unknown.

    Strips Harte-style extension parens `(...)`, bass/inversion `/...`, and
    surrounding whitespace before alias lookup.
    """
    q = raw.strip()
    if "/" in q:
        q = q.split("/", 1)[0]
    if "(" in q:
        q = q.split("(", 1)[0]
    q = q.strip()
    return _QUALITY_ALIASES.get(q)


def build_vocab(token_streams) -> dict[str, int]:
    """Build a token->id map from observed streams. Specials get low ids."""
    vocab = {t: i for i, t in enumerate(SPECIALS)}
    for stream in token_streams:
        for tok in stream:
            if tok not in vocab:
                vocab[tok] = len(vocab)
    return vocab
