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
