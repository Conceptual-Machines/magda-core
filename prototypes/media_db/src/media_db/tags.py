"""Default zero-shot tag taxonomy. Each entry becomes a CLAP text prompt; the
top-N tags above a confidence threshold are written to media_tag.

Validating which tags fire reliably (and tuning the threshold) is the explicit
exit criterion before C++ integration. Edit the list, rescan, and review.

Tags are scored by cosine similarity between the audio embedding and the text
embedding of the formatted prompt. Format string is template-friendly so the
taxonomy stays terse but the prompts stay descriptive.
"""

from __future__ import annotations

from pathlib import Path

import yaml

PROMPT_TEMPLATE = "the sound of {tag}"

DEFAULT_TAGS: list[str] = [
    # drums
    "a kick drum",
    "a snare drum",
    "a clap",
    "a hi-hat",
    "a cymbal",
    "a tom drum",
    "a percussion loop",
    "a drum loop",
    "a 808 bass drum",
    # bass and lead
    "a sub bass",
    "a synth bass",
    "an acid bass",
    "a synth lead",
    "a synth pad",
    "a synth pluck",
    "an arpeggio",
    # acoustic
    "a piano",
    "an electric piano",
    "an organ",
    "an acoustic guitar",
    "an electric guitar",
    "strings",
    "brass",
    "woodwinds",
    "a vocal",
    "a vocal chop",
    # fx
    "a sound effect",
    "an impact",
    "a riser",
    "a downlifter",
    "a noise sweep",
    "an ambience",
    "a foley sound",
    # texture / mood descriptors
    "a dark sound",
    "a bright sound",
    "a warm sound",
    "a metallic sound",
    "a distorted sound",
    "a clean sound",
    "a lo-fi sound",
    "a glitchy sound",
]


def load(path: Path | None) -> list[str]:
    if path is None:
        return list(DEFAULT_TAGS)
    data = yaml.safe_load(path.read_text())
    if not isinstance(data, list) or not all(isinstance(x, str) for x in data):
        raise ValueError(f"{path}: expected a YAML list of strings")
    return data


def prompts(tags: list[str]) -> list[str]:
    return [PROMPT_TEMPLATE.format(tag=t) for t in tags]
