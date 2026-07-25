"""Template-based synthetic dataset generator: NL request -> MAGDA DSL.

v0 strategy (per the POC spec): templates first. Each generator samples a
structured intent, renders the canonical gold DSL from it (so labels are
correct by construction), and renders a natural-language paraphrase from a
template bank. Later phases can swap the NL side for an LLM teacher's
paraphrases while keeping the same gold DSL.

Usage:
    python -m dataset.generate --n 500 --seed 7 --out data/train.jsonl
"""
from __future__ import annotations

import argparse
import json
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from magda_dsl import dsl, vocab  # noqa: E402

# ---------------------------------------------------------------------------
# Slot pools
# ---------------------------------------------------------------------------
TRACK_NAMES = [
    "Bass", "Drums", "Lead", "Pads", "Vocals", "Kick", "Snare", "Hats",
    "Reese Bass", "Sub Bass", "Pluck", "Arp", "Strings", "Brass", "Guitar",
    "Perc", "Keys", "Synth", "Sub", "Top Loop", "Vox", "Choir",
    # Diverse / invented names so name-tagging generalizes by position+context
    # (with unk-aug) instead of memorizing the canonical list.
    "Punchy", "Wubby", "Growl", "Rumble", "Zap", "Glue", "Air", "Texture",
    "Wide Pad", "Dusty Keys", "Night Bass", "Chop", "Riser", "Downlifter",
    "Sidechain Pad", "Ghost Snare", "Tape Vox", "Analog Lead", "Foley",
    "Bassline", "Melody", "Counter Melody", "Stab", "Sweep", "Impact",
    "Room Mics", "Overheads", "Shaker", "Tom Fill", "Rhodes",
]
# Plugin references are @alias tokens injected by the alias system; the model
# must generalize to ANY alias, so this pool is deliberately diverse (real +
# arbitrary). NL surface = "@" + alias; emitted DSL = "<alias>". The model
# never learns plugin identities — it tags the @token and passes it through.
PLUGIN_ALIASES = [
    "serum", "serum_2", "vital", "surge_xt", "diva", "massive", "ott",
    "pro_q_3", "pro_c_2", "reverb", "delay", "eq", "compressor", "1176",
    "valhalla", "phaseplant", "kontakt", "omnisphere", "saturn_2", "shaperbox",
    # arbitrary aliases — the registry supplies anything at runtime
    "fx_1", "synth_a", "my_bass", "lead_patch", "chan_7", "plugin_x", "vst_42",
]
COLOR_WORDS = list(vocab.COLORS.keys())
VOLUME_DB_VALUES = [-18, -12, -9, -6, -3, 0, 3, 6]
CLIP_NAMES = ["Intro", "Verse", "Chorus", "Bridge", "Drop", "Fill", "Loop", "Take 1"]
# Multi-word clip names — force the model to tag a 2-token NEW_NAME span in the
# select->rename chain (single-word names alone let it truncate to one token).
MULTIWORD_CLIP_NAMES = [
    "Bass Clip", "Drum Clip", "Vocal Take", "Verse Loop", "Chorus Part",
    "Intro Loop", "Drop Section", "Main Loop", "Lead Take", "Vocal Chop",
    "Second Verse", "Final Chorus", "Take Two", "Part One", "Backing Vox",
]
CLIP_TYPES = ["midi", "audio"]
CLIP_BAR_VALUES = [1, 2, 4, 8, 16]
PAN_VALUES = [
    ("hard left", -1.0),
    ("left", -0.5),
    ("slightly left", -0.25),
    ("center", 0.0),
    ("centre", 0.0),
    ("slightly right", 0.25),
    ("right", 0.5),
    ("hard right", 1.0),
]
# Note names (naturals + flats + MIDI numbers only; the tokenizer splits '#',
# so sharps are intentionally excluded from generated data).
PITCHES = ["C4", "D4", "E4", "F4", "G4", "A4", "B4", "C3", "G3", "C2",
           "Eb3", "Bb3", "60", "48", "72"]
SEMITONE_VALUES = [1, 2, 3, 5, 7, 12]
VELOCITY_VALUES = [40, 60, 80, 100, 110, 127]
LENGTH_VALUES = [0.25, 0.5, 1, 2]
GRID_WORDS = [("16th", 0.25), ("8th", 0.5), ("quarter", 1.0), ("32nd", 0.125)]
CLIP_INDEX_VALUES = [0, 1, 2, 3]
MOVE_INDEX_VALUES = [1, 2, 3, 4, 5]
STRENGTH_VALUES = [0.3, 0.5, 0.7, 1.0]
GROOVE_TEMPLATES = ["Basic 8th Swing", "16th Swing", "Shuffle", "MPC Swing",
                    "Laid Back", "Hard Swing"]


def _alias_pair(alias: str):
    """alias 'serum' -> ('@serum' surface, '<serum>' DSL token)."""
    return "@" + alias, "<" + alias + ">"


# ---------------------------------------------------------------------------
# Per-command generators -> (nl_text, actions)
# ---------------------------------------------------------------------------
# Track-TYPE descriptors — decorative words the DSL ignores; the model must
# learn that a name after "called/named" overrides the descriptor.
DESCRIPTORS = ["drum", "bass", "synth", "lead", "vocal", "perc", "pad", "keys",
               "guitar", "sub", "fx", "midi", "audio"]


def gen_create_track(r: random.Random):
    name = r.choice(TRACK_NAMES)
    templates = [
        f"create a {name.lower()} track",
        f"add a new {name.lower()} track",
        f"make a {name.lower()} track",
        f"new track called {name}",
        f"create a track named {name}",
    ]
    return r.choice(templates), [{"type": "create_track", "name": name}]


def gen_create_named(r: random.Random):
    """A descriptor word PLUS a distinct custom name: 'add a drum track called
    Punchy' -> name=Punchy (the descriptor is decorative)."""
    desc = r.choice(DESCRIPTORS)
    name = r.choice([n for n in TRACK_NAMES
                     if desc not in n.lower() and n.lower() not in desc])
    templates = [
        f"add a {desc} track called {name}",
        f"create a {desc} track named {name}",
        f"make a {desc} lane called {name}",
        f"new {desc} track called {name}",
        f"spin up a {desc} track named {name}",
        f"add a new {desc} track called {name}",
        f"set up a {desc} track and label it {name}",
        f"set up a {desc} track and call it {name}",
        f"add a {desc} track and name it {name}",
        f"create a {desc} track and label it {name}",
        f"make a {desc} track and call it {name}",
        f"new {desc} track, label it {name}",
    ]
    return r.choice(templates), [{"type": "create_track", "name": name}]


def gen_create_with_plugins(r: random.Random):
    """New track carrying devices, named or not.

    The unnamed branch matters more than it looks. Without it every
    create_track-with-plugins example carries a track name, so the only
    unnamed "new X with @alias" shape anywhere in the data is gen_create_rack's
    "new rack with @serum" — and "new track with @polysynth" lands on it,
    yielding track(name="").rack.new() (a rack on the *selected* track) instead
    of a new track. Empty name renders as new=true, which the interpreter
    creates rather than resolving against the selection.
    """
    named = r.random() < 0.7
    name = r.choice(TRACK_NAMES) if named else ""
    aliases = [r.choice(PLUGIN_ALIASES)]
    if r.random() < 0.5:
        other = r.choice([a for a in PLUGIN_ALIASES if a != aliases[0]])
        aliases.append(other)
    surfaces, tokens = zip(*(_alias_pair(a) for a in aliases))
    joined = " and ".join(surfaces)
    if named:
        templates = [
            f"create a {name.lower()} track with {joined}",
            f"make a {name.lower()} track and add {joined}",
            f"new {name.lower()} track with {joined}",
            f"spin up a {name.lower()} track loaded with {joined}",
        ]
    else:
        templates = [
            f"new track with {joined}",
            f"create a track with {joined}",
            f"add a track with {joined}",
            f"make a track with {joined}",
            f"new track loaded with {joined}",
            f"create a new track with {joined}",
        ]
    return r.choice(templates), [{"type": "create_track", "name": name, "plugins": list(tokens)}]


def gen_create_rack(r: random.Random):
    """Create a rack, optionally on a named track and optionally pre-filled with
    devices. Empty name -> the selected track (interpreter targets selection).
    Devices chain onto rack.new so they land inside the rack."""
    on_track = r.random() < 0.5
    name = r.choice(TRACK_NAMES) if on_track else ""
    action = {"type": "create_rack", "name": name}

    if r.random() < 0.6:
        aliases = [r.choice(PLUGIN_ALIASES)]
        if r.random() < 0.4:
            aliases.append(r.choice([a for a in PLUGIN_ALIASES if a != aliases[0]]))
        surfaces, tokens = zip(*(_alias_pair(a) for a in aliases))
        action["plugins"] = list(tokens)
        joined = " and ".join(surfaces)
        if on_track:
            templates = [
                f"create a rack with {joined} on the {name.lower()} track",
                f"add a rack with {joined} to {name}",
                f"make a parallel rack with {joined} on {name}",
                f"new rack on {name} with {joined}",
            ]
        else:
            templates = [
                f"create a rack with {joined}",
                f"add a rack with {joined}",
                f"make a parallel rack with {joined}",
                f"new rack with {joined}",
            ]
    else:
        if on_track:
            templates = [
                f"add a rack to the {name.lower()} track",
                f"create a rack on {name}",
                f"put a rack on the {name.lower()} track",
                f"new rack on the {name.lower()} track",
            ]
        else:
            templates = [
                "create a rack",
                "add a rack",
                "make a new rack",
                "add a parallel rack",
                "create a new rack",
            ]
    return r.choice(templates), [action]


def gen_add_plugin(r: random.Random):
    name = r.choice(TRACK_NAMES)
    # ~30% of the time add two plugins at once. This is deliberately kept
    # WITHOUT the word "rack" so the model separates "add @a and @b to X"
    # (add_plugin, one fx.add per plugin) from "add a rack with @a and @b to X"
    # (create_rack). The disambiguating signal is the word "rack".
    if r.random() < 0.3:
        aliases = [r.choice(PLUGIN_ALIASES)]
        aliases.append(r.choice([a for a in PLUGIN_ALIASES if a != aliases[0]]))
        surfaces, tokens = zip(*(_alias_pair(a) for a in aliases))
        joined = " and ".join(surfaces)
        templates = [
            f"add {joined} to the {name.lower()} track",
            f"put {joined} on {name}",
            f"load {joined} on the {name.lower()} track",
            f"insert {joined} on {name}",
        ]
        actions = [{"type": "add_plugin", "name": name, "plugin": t} for t in tokens]
        return r.choice(templates), actions

    at, token = _alias_pair(r.choice(PLUGIN_ALIASES))
    templates = [
        f"add {at} to the {name.lower()} track",
        f"put {at} on {name}",
        f"load {at} on the {name.lower()} track",
        f"insert {at} on {name}",
        f"throw {at} on the {name.lower()} track",
    ]
    return r.choice(templates), [{"type": "add_plugin", "name": name, "plugin": token}]


def gen_rename_track(r: random.Random):
    old = r.choice(TRACK_NAMES)
    # avoid pairs where one name contains the other (ambiguous span tagging)
    new = r.choice([n for n in TRACK_NAMES
                    if n != old and n.lower() not in old.lower()
                    and old.lower() not in n.lower()])
    templates = [
        f"rename track {old} to {new}",
        f"rename the {old.lower()} track to {new}",
        f"change {old} to {new}",
    ]
    return r.choice(templates), [{"type": "rename_track", "name": old, "new_name": new}]


def gen_delete_track(r: random.Random):
    name = r.choice(TRACK_NAMES)
    templates = [
        f"delete the {name.lower()} track",
        f"remove track {name}",
        f"delete {name}",
    ]
    return r.choice(templates), [{"type": "delete_track", "name": name}]


def gen_mute_track(r: random.Random):
    name = r.choice(TRACK_NAMES)
    templates = [f"mute {name}", f"mute the {name.lower()} track", f"silence {name}"]
    return r.choice(templates), [{"type": "mute_track", "name": name}]


def gen_solo_track(r: random.Random):
    name = r.choice(TRACK_NAMES)
    templates = [f"solo {name}", f"solo the {name.lower()} track", f"isolate {name}"]
    return r.choice(templates), [{"type": "solo_track", "name": name}]


def gen_set_volume(r: random.Random):
    name = r.choice(TRACK_NAMES)
    db = r.choice(VOLUME_DB_VALUES)
    templates = [
        f"set {name} volume to {db} dB",
        f"turn the {name.lower()} track to {db} dB",
        f"make {name} {db} dB",
        f"set volume of {name} to {db} dB",
        f"level {name} at {db} dB",
        f"bring the {name.lower()} track to {db} dB",
        f"adjust {name} volume to {db} dB",
    ]
    return r.choice(templates), [{"type": "set_track_volume", "name": name, "volume_db": db}]


def gen_set_pan(r: random.Random):
    name = r.choice(TRACK_NAMES)
    phrase, pan = r.choice(PAN_VALUES)
    templates = [
        f"pan {name} {phrase}",
        f"set {name} pan to {phrase}",
        f"move the {name.lower()} track {phrase}",
        f"put {name} {phrase} in the stereo field",
        f"place {name.lower()} {phrase}",
        f"send the {name.lower()} track {phrase}",
        f"set pan of {name} to {phrase}",
    ]
    return r.choice(templates), [{"type": "set_track_pan", "name": name, "pan": pan}]


def gen_set_color(r: random.Random):
    name = r.choice(TRACK_NAMES)
    cw = r.choice(COLOR_WORDS)
    hexv = vocab.COLORS[cw]
    templates = [
        f"make {name} {cw}",
        f"color the {name.lower()} track {cw}",
        f"set {name} colour to {cw}",
        f"color code {name.lower()} {cw}",
        f"make the {name.lower()} track {cw}",
        f"turn the {name.lower()} track {cw}",
        f"set the {name.lower()} track to {cw}",
        f"paint {name} {cw}",
    ]
    return r.choice(templates), [{"type": "set_track_color", "name": name, "colour": hexv}]


def gen_group_tracks(r: random.Random):
    n = r.randint(2, 4)
    start = r.randint(1, 4)
    ids = list(range(start, start + n))
    group = r.choice(["Drums", "Bass Bus", "Vocals", "Rhythm", "Synths", "Perc"])
    id_str = ", ".join(str(i) for i in ids[:-1]) + f" and {ids[-1]}"
    templates = [
        f"group tracks {id_str} as {group}",
        f"group tracks {id_str} into {group}",
        f"put tracks {id_str} in a group called {group}",
    ]
    return r.choice(templates), [{"type": "group_tracks", "ids": ids, "name": group}]


def gen_select_all_clips(r: random.Random):
    name = r.choice(TRACK_NAMES)
    templates = [
        f"select all clips on {name}",
        f"select every clip on the {name.lower()} track",
        f"select all clips in {name}",
        f"highlight all clips on the {name.lower()} track",
        f"select all regions on {name}",
        f"select every region in the {name.lower()} track",
        f"grab all clips from {name}",
        f"select all items on the {name.lower()} track",
    ]
    return r.choice(templates), [{"type": "select_all_clips", "name": name}]


def gen_select_all_clips_rename(r: random.Random):
    name = r.choice(TRACK_NAMES)
    new_name = r.choice(CLIP_NAMES + MULTIWORD_CLIP_NAMES)
    templates = [
        f"select all clips on {name} and rename them to {new_name}",
        f"rename every clip on the {name.lower()} track to {new_name}",
        f"select all clips in {name} then call them {new_name}",
        f"highlight all clips on the {name.lower()} track and rename to {new_name}",
    ]
    return r.choice(templates), [{"type": "select_all_clips_rename", "name": name, "new_name": new_name}]


def gen_select_clips_named(r: random.Random):
    name = r.choice(TRACK_NAMES)
    clip = r.choice(CLIP_NAMES)
    templates = [
        f"select clips named {clip} on {name}",
        f"select the clip called {clip} on the {name.lower()} track",
        f"highlight clips named {clip} in {name}",
        f"grab the {clip} clips from the {name.lower()} track",
    ]
    return r.choice(templates), [{"type": "select_clips_named", "name": name, "clip_name": clip}]


def gen_select_clips_type(r: random.Random):
    name = r.choice(TRACK_NAMES)
    clip_type = r.choice(CLIP_TYPES)
    templates = [
        f"select {clip_type} clips on {name}",
        f"select all {clip_type} clips in the {name.lower()} track",
        f"highlight {clip_type} regions on {name}",
        f"grab {clip_type} clips from the {name.lower()} track",
    ]
    return r.choice(templates), [{"type": "select_clips_type", "name": name, "clip_type": clip_type}]


def gen_select_clips_longer_than(r: random.Random):
    name = r.choice(TRACK_NAMES)
    bars = r.choice(CLIP_BAR_VALUES)
    templates = [
        f"select clips longer than {bars} bars on {name}",
        f"select all clips over {bars} bars in the {name.lower()} track",
        f"highlight regions longer than {bars} bars on {name}",
    ]
    return r.choice(templates), [{"type": "select_clips_longer_than", "name": name, "bars": bars}]


def gen_select_clips_shorter_than(r: random.Random):
    name = r.choice(TRACK_NAMES)
    bars = r.choice(CLIP_BAR_VALUES)
    bw = "bar" if bars == 1 else "bars"
    templates = [
        f"select clips shorter than {bars} {bw} on {name}",
        f"select all clips under {bars} {bw} in the {name.lower()} track",
        f"highlight regions shorter than {bars} {bw} on {name}",
        f"select clips under {bars} {bw} on {name}",
        f"select clips less than {bars} {bw} long in {name}",
    ]
    return r.choice(templates), [{"type": "select_clips_shorter_than", "name": name, "bars": bars}]


def gen_select_clips_starting_after(r: random.Random):
    name = r.choice(TRACK_NAMES)
    bar = r.choice(CLIP_BAR_VALUES)
    templates = [
        f"select clips after bar {bar} on {name}",
        f"select clips starting after bar {bar} in the {name.lower()} track",
        f"highlight regions from bar {bar} onward on {name}",
    ]
    return r.choice(templates), [{"type": "select_clips_starting_after", "name": name, "bar": bar}]


def gen_select_clips_starting_before(r: random.Random):
    name = r.choice(TRACK_NAMES)
    bar = r.choice(CLIP_BAR_VALUES)
    templates = [
        f"select clips before bar {bar} on {name}",
        f"select clips starting before bar {bar} in the {name.lower()} track",
        f"highlight regions up to bar {bar} on {name}",
    ]
    return r.choice(templates), [{"type": "select_clips_starting_before", "name": name, "bar": bar}]


# --- clip ops --------------------------------------------------------------
def gen_clip_new(r: random.Random):
    name = r.choice(TRACK_NAMES)
    bars = r.choice(CLIP_BAR_VALUES)
    templates = [
        f"add a {bars} bar clip to {name}",
        f"create a {bars} bar clip on the {name.lower()} track",
        f"new {bars} bar clip on {name}",
        f"put a {bars} bar clip on the {name.lower()} track",
    ]
    return r.choice(templates), [{"type": "clip_new", "name": name, "length_bars": bars}]


def gen_clip_rename(r: random.Random):
    name = r.choice(TRACK_NAMES)
    clip = r.choice(CLIP_NAMES)
    templates = [
        f"rename the clip on {name} to {clip}",
        f"rename the selected clips on the {name.lower()} track to {clip}",
        f"call the clip on {name} {clip}",
    ]
    return r.choice(templates), [{"type": "clip_rename", "name": name, "clip_name": clip}]


def gen_clip_delete(r: random.Random):
    name = r.choice(TRACK_NAMES)
    idx = r.choice(CLIP_INDEX_VALUES)
    templates = [
        f"delete clip {idx} on {name}",
        f"remove clip {idx} from the {name.lower()} track",
        f"delete the clip at index {idx} on {name}",
    ]
    return r.choice(templates), [{"type": "clip_delete", "name": name, "index": idx}]


def gen_track_move(r: random.Random):
    name = r.choice(TRACK_NAMES)
    idx = r.choice(MOVE_INDEX_VALUES)
    templates = [
        f"move {name} to position {idx}",
        f"move the {name.lower()} track to position {idx}",
        f"move {name} to slot {idx}",
    ]
    return r.choice(templates), [{"type": "track_move", "name": name, "index": idx}]


# --- MIDI note ops ---------------------------------------------------------
def gen_notes_delete(r: random.Random):
    name = r.choice(TRACK_NAMES)
    templates = [
        f"delete the notes on {name}",
        f"clear the notes on the {name.lower()} track",
        f"remove all notes from {name}",
    ]
    return r.choice(templates), [{"type": "notes_delete", "name": name}]


def gen_notes_transpose(r: random.Random):
    name = r.choice(TRACK_NAMES)
    semi = r.choice(SEMITONE_VALUES)
    if r.random() < 0.5:
        nl = r.choice([f"transpose the notes on {name} up {semi} semitones",
                       f"shift the {name.lower()} notes up {semi} semitones"])
        val = semi
    else:
        nl = r.choice([f"transpose the notes on {name} down {semi} semitones",
                       f"move the {name.lower()} notes down {semi} semitones"])
        val = -semi
    return nl, [{"type": "notes_transpose", "name": name, "semitones": val}]


def gen_notes_set_velocity(r: random.Random):
    name = r.choice(TRACK_NAMES)
    v = r.choice(VELOCITY_VALUES)
    templates = [
        f"set the note velocity on {name} to {v}",
        f"set velocity of the {name.lower()} notes to {v}",
        f"make the {name.lower()} notes velocity {v}",
        f"set the {name.lower()} note velocity to {v}",
        f"change the {name.lower()} note velocity to {v}",
    ]
    return r.choice(templates), [{"type": "notes_set_velocity", "name": name, "value": v}]


def gen_notes_resize(r: random.Random):
    name = r.choice(TRACK_NAMES)
    ln = r.choice(LENGTH_VALUES)
    templates = [
        f"set the note length on {name} to {ln} beats",
        f"resize the {name.lower()} notes to {ln} beats",
        f"set the {name.lower()} note length to {ln} beats",
        f"make the {name.lower()} notes {ln} beats long",
        f"change the {name.lower()} note length to {ln} beats",
    ]
    return r.choice(templates), [{"type": "notes_resize", "name": name, "length": ln}]


def gen_notes_quantize(r: random.Random):
    name = r.choice(TRACK_NAMES)
    word, grid = r.choice(GRID_WORDS)
    templates = [
        f"quantize the notes on {name} to {word} notes",
        f"quantize the {name.lower()} notes to {word}s",
        f"snap the {name.lower()} notes to {word} notes",
    ]
    return r.choice(templates), [{"type": "notes_quantize", "name": name, "grid": grid}]


def gen_notes_set_pitch(r: random.Random):
    name = r.choice(TRACK_NAMES)
    pitch = r.choice(PITCHES)
    templates = [
        f"set the note pitch on {name} to {pitch}",
        f"set the {name.lower()} notes to {pitch}",
        f"set the {name.lower()} note pitch to {pitch}",
    ]
    return r.choice(templates), [{"type": "notes_set_pitch", "name": name, "pitch": pitch}]


def gen_notes_select_pitch(r: random.Random):
    name = r.choice(TRACK_NAMES)
    pitch = r.choice(PITCHES)
    templates = [
        f"select all {pitch} notes on {name}",
        f"select the {pitch} notes on the {name.lower()} track",
    ]
    return r.choice(templates), [{"type": "notes_select_pitch", "name": name, "pitch": pitch}]


def gen_notes_select_velocity_above(r: random.Random):
    name = r.choice(TRACK_NAMES)
    v = r.choice(VELOCITY_VALUES)
    templates = [
        f"select notes on {name} with velocity above {v}",
        f"select the {name.lower()} notes louder than {v}",
    ]
    return r.choice(templates), [{"type": "notes_select_velocity_above", "name": name, "value": v}]


def gen_notes_select_velocity_below(r: random.Random):
    name = r.choice(TRACK_NAMES)
    v = r.choice(VELOCITY_VALUES)
    templates = [
        f"select notes on {name} with velocity below {v}",
        f"select the {name.lower()} notes quieter than {v}",
    ]
    return r.choice(templates), [{"type": "notes_select_velocity_below", "name": name, "value": v}]


# --- groove (timing/swing) -------------------------------------------------
def gen_groove_set(r: random.Random):
    template = r.choice(GROOVE_TEMPLATES)
    strength = r.choice(STRENGTH_VALUES)
    templates = [
        f"set the groove to {template} with strength {strength}",
        f"apply the {template} groove at strength {strength}",
        f"use the {template} groove at strength {strength}",
        f"use the {template} groove with strength {strength}",
        f"set groove {template} strength {strength}",
        f"apply {template} at {strength} strength",
    ]
    return r.choice(templates), [{"type": "groove_set", "template": template, "strength": strength}]


def gen_groove_list(r: random.Random):
    # Combinatorial phrasings so dedup keeps plenty of uniques and this rare
    # (parameter-less) intent actually trains. Kept general so it generalises
    # to the hand-authored testset phrasings without memorising them.
    verb = r.choice(["list", "show", "show me", "display", "what are",
                     "which", "give me"])
    obj = r.choice(["grooves", "the grooves", "groove templates",
                    "the groove templates", "available grooves",
                    "groove presets", "the groove list", "all grooves"])
    tail = r.choice(["", "", " available", " do we have", " are there"])
    text = f"{verb} {obj}{tail}".strip()
    return text, [{"type": "groove_list"}]


def gen_select_clips_length_at_least(r: random.Random):
    name = r.choice(TRACK_NAMES)
    bars = r.choice(CLIP_BAR_VALUES)
    bw = "bar" if bars == 1 else "bars"
    templates = [
        f"select clips at least {bars} {bw} in {name}",
        f"select clips {bars} {bw} or longer on {name}",
        f"select clips {bars} {bw} and up in the {name.lower()} track",
        f"select all clips no shorter than {bars} {bw} on {name}",
    ]
    return r.choice(templates), [{"type": "select_clips_length_at_least", "name": name, "bars": bars}]


def gen_select_clips_length_at_most(r: random.Random):
    name = r.choice(TRACK_NAMES)
    bars = r.choice(CLIP_BAR_VALUES)
    bw = "bar" if bars == 1 else "bars"
    templates = [
        f"select clips at most {bars} {bw} in {name}",
        f"select clips {bars} {bw} or shorter on {name}",
        f"select clips up to {bars} {bw} in the {name.lower()} track",
        f"select clips with length at most {bars} {bw} on {name}",
        f"select all clips no longer than {bars} {bw} on {name}",
    ]
    return r.choice(templates), [{"type": "select_clips_length_at_most", "name": name, "bars": bars}]


def gen_select_clips_length_exactly(r: random.Random):
    name = r.choice(TRACK_NAMES)
    bars = r.choice(CLIP_BAR_VALUES)
    bw = "bar" if bars == 1 else "bars"
    templates = [
        f"select clips exactly {bars} {bw} in {name}",
        f"select clips that are {bars} {bw} long on {name}",
        f"select clips with length {bars} {bw} in the {name.lower()} track",
        f"select all clips of exactly {bars} {bw} on {name}",
    ]
    return r.choice(templates), [{"type": "select_clips_length_exactly", "name": name, "bars": bars}]


def gen_select_clips_not_named(r: random.Random):
    name = r.choice(TRACK_NAMES)
    clip = r.choice(CLIP_NAMES)
    templates = [
        f"select clips not named {clip} on {name}",
        f"select clips except {clip} in the {name.lower()} track",
        f"select all clips that aren't called {clip} on {name}",
        f"select clips other than {clip} in {name}",
    ]
    return r.choice(templates), [{"type": "select_clips_not_named", "name": name, "clip_name": clip}]


GENERATORS = [
    (gen_create_track, 0.08),
    (gen_create_named, 0.05),
    (gen_create_with_plugins, 0.08),
    (gen_create_rack, 0.06),
    (gen_add_plugin, 0.09),
    (gen_rename_track, 0.05),
    (gen_delete_track, 0.04),
    (gen_mute_track, 0.04),
    (gen_solo_track, 0.03),
    (gen_set_volume, 0.05),
    (gen_set_pan, 0.05),
    (gen_set_color, 0.04),
    (gen_group_tracks, 0.04),
    (gen_select_all_clips, 0.03),
    (gen_select_all_clips_rename, 0.02),
    (gen_select_clips_named, 0.02),
    (gen_select_clips_type, 0.02),
    (gen_select_clips_longer_than, 0.02),
    (gen_select_clips_shorter_than, 0.02),
    (gen_select_clips_starting_after, 0.02),
    (gen_select_clips_starting_before, 0.02),
    (gen_select_clips_length_at_least, 0.02),
    (gen_select_clips_length_at_most, 0.02),
    (gen_select_clips_length_exactly, 0.02),
    (gen_select_clips_not_named, 0.02),
    # new command coverage
    (gen_clip_new, 0.03),
    (gen_clip_rename, 0.02),
    (gen_clip_delete, 0.02),
    (gen_track_move, 0.02),
    (gen_notes_delete, 0.02),
    (gen_notes_transpose, 0.03),
    (gen_notes_set_velocity, 0.02),
    (gen_notes_resize, 0.02),
    (gen_notes_quantize, 0.02),
    (gen_notes_set_pitch, 0.02),
    (gen_notes_select_pitch, 0.02),
    (gen_notes_select_velocity_above, 0.015),
    (gen_notes_select_velocity_below, 0.015),
    (gen_groove_set, 0.02),
    (gen_groove_list, 0.02),
]


def make_english_example(r: random.Random):
    funcs = [g for g, _ in GENERATORS]
    weights = [w for _, w in GENERATORS]
    gen = r.choices(funcs, weights=weights, k=1)[0]
    nl, actions = gen(r)
    return {"lang": "en", "input": nl, "output": dsl.render(actions), "actions": actions}


def _load_inputs(path):
    """Return the set of `input` strings in a jsonl file (for leakage guard)."""
    out = set()
    if path and os.path.exists(path):
        with open(path, "r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if line:
                    out.add(json.loads(line)["input"])
    return out


def _write(path, rows):
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        for row in rows:
            f.write(json.dumps(row, ensure_ascii=False) + "\n")


def _augment(rows, teacher, n, seen, excluded, workers, typo=False):
    """LLM-teacher paraphrases of `rows`, validated + deduped. Returns new rows.
    `seen`/`excluded` are updated so paraphrases never collide with existing
    inputs, the test set, or each other (across train and val)."""
    from concurrent.futures import ThreadPoolExecutor
    out = []

    def work(row):
        return row, teacher.paraphrases(row["input"], row["actions"], row["output"], n, typo)

    with ThreadPoolExecutor(max_workers=workers) as ex:
        for i, (row, paras) in enumerate(ex.map(work, rows), 1):
            for p in paras:
                if p in seen or p in excluded:
                    continue
                seen.add(p)
                out.append({"lang": "en", "input": p,
                            "output": row["output"], "actions": row["actions"]})
            if i % 100 == 0:
                print(f"  teacher: {i}/{len(rows)} base examples, +{len(out)} paraphrases")
                teacher.save()  # periodic: progress survives a crash
    teacher.save()
    return out


def main():
    here = os.path.dirname(__file__)
    ap = argparse.ArgumentParser()
    ap.add_argument("--n", type=int, default=500, help="number of ENGLISH procedural train examples")
    ap.add_argument("--seed", type=int, default=7)
    ap.add_argument("--out", default=os.path.join(here, "..", "data", "train.jsonl"))
    ap.add_argument("--val", type=int, default=0,
                    help="hold out this many English examples into a disjoint val split")
    ap.add_argument("--val-out", default=os.path.join(here, "..", "data", "val.jsonl"))
    ap.add_argument("--exclude", default=os.path.join(here, "..", "eval", "testset.jsonl"),
                    help="jsonl whose inputs are kept OUT of train/val (leakage guard); '' to disable")
    ap.add_argument("--teacher", type=int, default=0,
                    help="LLM-teacher paraphrases per base example (0 = templates only)")
    ap.add_argument("--teacher-typo", type=int, default=0,
                    help="additional typo'd paraphrases per base example (0 = none)")
    ap.add_argument("--teacher-model", default="deepseek-chat")
    ap.add_argument("--teacher-workers", type=int, default=8)
    args = ap.parse_args()

    r = random.Random(args.seed)

    # English inputs that must never appear in train/val (the eval test set).
    excluded = _load_inputs(args.exclude)
    n_excluded = 0

    # ---- English: procedural, deduped, leakage-guarded -------------------
    seen = set()
    en_rows = []
    target = args.n + args.val
    attempts = 0
    while len(en_rows) < target and attempts < target * 80:
        attempts += 1
        ex = make_english_example(r)
        if ex["input"] in seen:
            continue
        if ex["input"] in excluded:
            n_excluded += 1
            continue
        seen.add(ex["input"])
        en_rows.append(ex)

    # Split English into val (first --val) and train (rest), disjoint by input.
    val_rows = en_rows[:args.val]
    train_en = en_rows[args.val:]

    # ---- LLM-teacher paraphrase augmentation (phrasing diversity) --------
    # Applied per-split so paraphrases of a val example stay in val (no leak).
    if args.teacher > 0 or args.teacher_typo > 0:
        from dataset.teacher import Teacher, load_api_key
        key = load_api_key()
        if not key:
            sys.exit("--teacher set but DEEPSEEK_API_KEY not found (env or repo-root .env)")
        cache = os.path.join(here, "..", "data", ".teacher_cache.json")
        teacher = Teacher(key, model=args.teacher_model, cache_path=cache)
        base_train, base_val = list(train_en), list(val_rows)

        def augment(n, typo):
            nonlocal train_en, val_rows
            kind = "typo" if typo else "clean"
            print(f"teacher: {kind} paraphrasing (n={n}) ...")
            train_en += _augment(base_train, teacher, n, seen, excluded, args.teacher_workers, typo)
            if base_val:
                val_rows += _augment(base_val, teacher, n, seen, excluded, args.teacher_workers, typo)

        if args.teacher > 0:
            augment(args.teacher, False)
        if args.teacher_typo > 0:
            augment(args.teacher_typo, True)
        print(f"teacher: train now {len(train_en)}, val now {len(val_rows)}")

    train_rows = list(train_en)
    train_path = os.path.abspath(args.out)
    _write(train_path, train_rows)
    extra = f" [excluded {n_excluded} test-set inputs]" if excluded else ""
    print(f"train: {len(train_rows)} examples (en){extra} -> {train_path}")

    if args.val:
        val_path = os.path.abspath(args.val_out)
        _write(val_path, val_rows)
        print(f"val:   {len(val_rows)} examples (en) -> {val_path}")


if __name__ == "__main__":
    main()
