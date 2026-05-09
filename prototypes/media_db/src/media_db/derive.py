"""Derive structured categorical labels (shape, family, tonal) from features +
tags. These power UI filters that producers expect: "show me only one-shots /
loops / pads", "show me drums in 120-130 BPM", etc.

All three derivations are cheap rules over signals we already have. No ML.
The C++ runtime reimplements the same rules — they're stable and the input
signals (duration_s, transient_density, top tag, key_confidence) port cleanly.
"""

from __future__ import annotations

from .features.audio_features import AudioFeatures, FLATNESS_THRESHOLD
from .tags import TAG_FAMILY

# Family is only assigned when the top tag scores above this floor — otherwise
# the file is treated as "unknown" rather than guessed at random.
FAMILY_TAG_FLOOR = 0.20


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


def family(top_tags: list[tuple[str, float]]) -> str:
    """Pick the highest-scoring tag whose prompt maps to a real instrument family
    (skipping 'texture' descriptors so 'warm sound' doesn't beat 'a synth pad').
    Returns 'unknown' if nothing fires hard enough."""
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
