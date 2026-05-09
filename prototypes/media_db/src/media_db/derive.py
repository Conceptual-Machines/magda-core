"""Derive structured categorical labels (shape, family, tonal) from features +
tags. These power UI filters that producers expect: "show me only one-shots /
loops / pads", "show me drums in 120-130 BPM", etc.

All three derivations are cheap rules over signals we already have. No ML.
The C++ runtime reimplements the same rules — they're stable and the input
signals (duration_s, transient_density, top tag, key_confidence) port cleanly.
"""

from __future__ import annotations

import re
from pathlib import Path

from .features.audio_features import AudioFeatures, FLATNESS_THRESHOLD
from .tags import TAG_FAMILY

# Family is only assigned when the top tag scores above this floor — otherwise
# the file is treated as "unknown" rather than guessed at random.
FAMILY_TAG_FLOOR = 0.20

# Path tokens that hint at a family. Sample packs are remarkably consistent
# about folder structure ("/Vocals/", "/Drums/Kicks/", "/FX/Risers/") and the
# evidence from the path is often more reliable than CLAP's audio embedding —
# a short percussive vocal hit sounds drum-like to CLAP but the producer who
# named the file "vocal_oneshot.wav" knew what they were making.
_PATH_FAMILY_KEYWORDS: dict[str, str] = {
    # vocals
    "vocal": "vocal", "vocals": "vocal", "vox": "vocal",
    "acapella": "vocal", "acapellas": "vocal", "voc": "vocal", "adlib": "vocal",
    "adlibs": "vocal",
    # drums
    "kick": "drum", "kicks": "drum", "snare": "drum", "snares": "drum",
    "clap": "drum", "claps": "drum", "hat": "drum", "hats": "drum",
    "hihat": "drum", "hihats": "drum", "tom": "drum", "toms": "drum",
    "cymbal": "drum", "cymbals": "drum", "ride": "drum", "rides": "drum",
    "crash": "drum", "perc": "drum", "percussion": "drum", "drum": "drum",
    "drums": "drum",
    # bass
    "bass": "bass", "808": "bass", "sub": "bass", "subbass": "bass",
    # lead / pluck
    "lead": "lead", "leads": "lead", "pluck": "lead", "plucks": "lead",
    "arp": "lead", "arps": "lead", "arpeggio": "lead",
    # pad
    "pad": "pad", "pads": "pad",
    # keys
    "piano": "keys", "pianos": "keys", "rhodes": "keys", "wurli": "keys",
    "organ": "keys", "organs": "keys", "ep": "keys", "keys": "keys",
    # guitar
    "guitar": "guitar", "guitars": "guitar", "gtr": "guitar", "gtrs": "guitar",
    # orchestral
    "strings": "orchestral", "brass": "orchestral", "horn": "orchestral",
    "horns": "orchestral", "violin": "orchestral", "cello": "orchestral",
    "woodwind": "orchestral", "woodwinds": "orchestral", "flute": "orchestral",
    # fx / foley
    "fx": "fx", "sfx": "fx", "riser": "fx", "risers": "fx",
    "downer": "fx", "downlifter": "fx", "impact": "fx", "impacts": "fx",
    "ambience": "fx", "ambiences": "fx", "ambient": "fx",
    "foley": "fx", "sweep": "fx", "sweeps": "fx",
}

_PATH_TOKEN_RE = re.compile(r"[_/\-.\s,()]+")


# --- Key parsing from filenames -------------------------------------------
#
# Producers explicitly encode keys in filenames: 'kick_C.wav', 'synth_F#m.wav',
# 'bass_Bbmin.wav', 'pad_Cmaj.wav'. When present, this is more reliable than
# chroma-correlation key detection — especially on short tonal one-shots where
# Krumhansl has minimal harmonic context to lock onto.

# Matches a single token like 'C', 'Cm', 'C#', 'F#m', 'Bb', 'Cmin', 'Cmaj'.
# Root letter is case-sensitive uppercase (avoids matching 'g' in 'guitar').
# Scale suffixes are explicit alternatives (no IGNORECASE because then [A-G]
# would catch 'g' too).
_KEY_TOKEN_RE = re.compile(
    r"^([A-G])"                                                  # root note
    r"([b#])?"                                                   # optional accidental
    r"("
    r"maj|MAJ|Maj|major|MAJOR|Major"
    r"|min|MIN|Min|minor|MINOR|Minor"
    r"|m|M"
    r")?$"
)

# Convert flat roots to their sharp equivalents so the column is consistent
# with the chroma analyzer's vocabulary (PITCH_CLASSES uses sharps).
_FLAT_TO_SHARP: dict[str, str] = {
    "Cb": "B", "Db": "C#", "Eb": "D#", "Fb": "E",
    "Gb": "F#", "Ab": "G#", "Bb": "A#",
}


def parse_key_from_path(path: str | Path) -> tuple[str, str | None] | None:
    """Look for an explicit key marker in the filename.

    Returns (root, scale) where scale is 'major', 'minor', or None when the
    filename gives only the root (e.g. 'kick_C.wav' — producer conventions
    treat a bare letter as 'no mode specified', not 'major'). Returns None
    if no key marker is found.

    Tries tokens from right to left so the *trailing* key wins when a
    filename mentions several pitches (e.g. 'C_to_F.wav' is in F)."""
    stem = Path(path).stem
    tokens = [t for t in re.split(r"[_\-. ]+", stem) if t]

    for i in range(len(tokens) - 1, -1, -1):
        m = _KEY_TOKEN_RE.match(tokens[i])
        if m is None:
            continue
        root_letter, accidental, scale_str = m.groups()

        # If the token had no inline scale, peek at the next token in case
        # the producer wrote 'C_minor' / 'F#_major' as separate tokens.
        if scale_str is None and i + 1 < len(tokens):
            nxt = tokens[i + 1].lower()
            if nxt in ("major", "maj"):
                scale_str = "maj"
            elif nxt in ("minor", "min"):
                scale_str = "min"

        return _normalize_key(root_letter, accidental, scale_str)
    return None


def _normalize_key(
    root_letter: str, accidental: str | None, scale_str: str | None
) -> tuple[str, str | None]:
    if accidental == "#":
        root = root_letter + "#"
        # B# / E# are theoretically valid but never used in sample-pack naming.
        # If we see one, fall back to the natural letter.
        if root in ("B#", "E#"):
            root = root_letter
    elif accidental == "b":
        root = _FLAT_TO_SHARP.get(root_letter + "b", root_letter)
    else:
        root = root_letter

    scale: str | None = None
    if scale_str:
        sl = scale_str.lower()
        if sl in ("m", "min", "minor"):
            scale = "minor"
        elif sl in ("maj", "major"):
            scale = "major"
        elif scale_str == "M":  # explicit uppercase M is major by convention
            scale = "major"

    return (root, scale)


def _resolve_for_inspection(path: str | Path) -> Path:
    """Resolve symlinks so the folder hierarchy of the *real* file is visible.
    Test corpora often symlink files into a flat directory; without resolving,
    the leaf-folder heuristic only sees the symlink's parent and misses the
    descriptive `/Snares/`, `/Vocals/` folders on the original disk."""
    p = Path(path)
    try:
        return p.resolve()
    except OSError:
        return p


def _chunks_leaf_first(path: Path) -> list[str]:
    """Path chunks ordered for keyword search: leaf folder first (most
    semantically precise), then up to the root, then the file stem last.
    Pack-name folders are deprioritized because they often contain genre
    words ('Drum & Bass Toolkit') that aren't instrument-family signals."""
    parents = list(path.parts[:-1])
    return list(reversed(parents)) + [path.stem]


def path_family_hint(path: str | Path) -> str | None:
    """Scan the path for instrument-family keywords. Returns the family or
    None. The leaf folder dominates because '/Snares/file.wav' is a stronger
    signal than 'LAUT Drum & Bass Toolkit' two folders up — the latter just
    names the pack's genre. Resolves symlinks so the real folder structure
    is visible."""
    p = _resolve_for_inspection(path)
    for chunk in _chunks_leaf_first(p):
        tokens = [t.lower() for t in _PATH_TOKEN_RE.split(chunk) if t]
        for tok in tokens:
            fam = _PATH_FAMILY_KEYWORDS.get(tok)
            if fam is not None:
                return fam
    return None


def path_tags(path: str | Path) -> list[tuple[str, float]]:
    """Emit short keyword tags found in the path. These are stamped into
    media_tag with source_model='path' so the UI shows them alongside CLAP
    tags and the FTS index picks them up. Confidence is 1.0 because filename
    evidence is essentially deterministic.

    Deduped, preserves leaf-first occurrence order so the more specific tags
    appear first."""
    p = _resolve_for_inspection(path)
    raw_tokens: list[str] = []
    for chunk in _chunks_leaf_first(p):
        for tok in _PATH_TOKEN_RE.split(chunk):
            if tok:
                raw_tokens.append(tok.lower())

    out: list[tuple[str, float]] = []
    seen: set[str] = set()
    for tok in raw_tokens:
        if tok in _PATH_FAMILY_KEYWORDS and tok not in seen:
            seen.add(tok)
            out.append((tok, 1.0))
    return out


def shape(feat: AudioFeatures) -> str:
    """one-shot | loop | sustained | unknown.

    Heuristic, calibrated for sample-pack content:
      - duration < 2.0s            → one-shot (drum hits, FX, plucks)
      - duration ≥ 2.0s, dense     → loop (steady transients, e.g. drum/perc loops)
      - duration ≥ 2.0s, sparse    → sustained (pads, drones, ambiences)
      - everything else            → loop (the safe default for medium-length material)
    """
    if feat.duration_s <= 0:
        return "unknown"
    if feat.duration_s < 2.0:
        return "one-shot"
    if feat.transient_density < 0.5:
        return "sustained"
    return "loop"


def family(top_tags: list[tuple[str, float]], path: str | Path | None = None) -> str:
    """Pick an instrument family. Path hint takes precedence over CLAP because
    filename evidence is more reliable than the audio model on short or
    ambiguous samples (a 0.5s vocal hit sounds drum-like to CLAP, but a
    producer who put it in /Vocals/ knew exactly what it was).

    Falls back to the highest-scoring CLAP tag whose prompt maps to a real
    instrument family (skipping 'texture' descriptors so 'warm sound' doesn't
    beat 'a synth pad'). Returns 'unknown' if neither path nor tags fire."""
    if path is not None:
        hint = path_family_hint(path)
        if hint is not None:
            return hint
    for tag, conf in top_tags:
        fam = TAG_FAMILY.get(tag)
        if fam is None or fam == "texture":
            continue
        if conf >= FAMILY_TAG_FLOOR:
            return fam
    return "unknown"


def tonal(feat: AudioFeatures) -> bool:
    """True if spectral flatness is low enough that the file has clear pitched
    content. Drums and noise have high flatness and return False."""
    return feat.spectral_flatness < FLATNESS_THRESHOLD
