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


# Map each prompt to a coarse instrument family. Used to derive media_file.family
# from the file's top-scoring tag. Texture descriptors map to 'texture' so a
# "warm sound" tag never silently overrides a real instrument tag.
TAG_FAMILY: dict[str, str] = {
    "a kick drum": "drum",
    "a snare drum": "drum",
    "a clap": "drum",
    "a hi-hat": "drum",
    "a cymbal": "drum",
    "a tom drum": "drum",
    "a percussion loop": "drum",
    "a drum loop": "drum",
    "a 808 bass drum": "drum",

    "a sub bass": "bass",
    "a synth bass": "bass",
    "an acid bass": "bass",

    "a synth lead": "lead",
    "a synth pluck": "lead",
    "an arpeggio": "lead",

    "a synth pad": "pad",

    "a piano": "keys",
    "an electric piano": "keys",
    "an organ": "keys",

    "an acoustic guitar": "guitar",
    "an electric guitar": "guitar",

    "strings": "orchestral",
    "brass": "orchestral",
    "woodwinds": "orchestral",

    "a vocal": "vocal",
    "a vocal chop": "vocal",

    "a sound effect": "fx",
    "an impact": "fx",
    "a riser": "fx",
    "a downlifter": "fx",
    "a noise sweep": "fx",
    "an ambience": "fx",
    "a foley sound": "fx",

    "a dark sound": "texture",
    "a bright sound": "texture",
    "a warm sound": "texture",
    "a metallic sound": "texture",
    "a distorted sound": "texture",
    "a clean sound": "texture",
    "a lo-fi sound": "texture",
    "a glitchy sound": "texture",
}


def load(path: Path | None) -> list[str]:
    if path is None:
        return list(DEFAULT_TAGS)
    data = yaml.safe_load(path.read_text())
    if not isinstance(data, list) or not all(isinstance(x, str) for x in data):
        raise ValueError(f"{path}: expected a YAML list of strings")
    return data


def prompts(tags: list[str]) -> list[str]:
    return [PROMPT_TEMPLATE.format(tag=t) for t in tags]
