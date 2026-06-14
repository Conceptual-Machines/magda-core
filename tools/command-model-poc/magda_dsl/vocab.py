"""Shared vocabulary for the MAGDA command-model POC.

This is the single source of truth for:
  - which POC command types are expressible in the SHIPPING DSL (and which
    are gaps that need a grammar extension before they can be modelled),
  - the plugin inventory (display name + NL aliases -> DSL alias token),
  - the colour palette (colour word -> hex).

The DSL the model must emit is the functional chained DSL defined in
magda/agents/dsl_grammar.hpp. At runtime the real plugin alias tokens come
from AliasRegistry; here we use a curated stand-in inventory so the synthetic
dataset is self-contained.
"""

# ---------------------------------------------------------------------------
# POC command surface
# ---------------------------------------------------------------------------
# Every command type from the POC spec, mapped to whether the current DSL
# grammar can express it. Gaps are excluded from the v0 dataset and flagged in
# the README so the grammar/interpreter can be extended deliberately.
SUPPORTED_COMMANDS = [
    "create_track",      # track(name=.., new=true)
    "add_plugin",        # ....fx.add(name=..)
    "rename_track",      # ....track.set(name=..)
    "delete_track",      # ....delete()
    "mute_track",        # ....track.set(mute=true)
    "solo_track",        # ....track.set(solo=true)
    "set_track_color",   # ....track.set(colour="#..")
    "group_tracks",      # ....track.group(name=.., tracks="1,2,3")
]

# In the spec but NOT expressible in the shipping DSL grammar today.
# Documented, not modelled in v0. Each needs a grammar + interpreter addition.
GRAMMAR_GAPS = {
    "duplicate_track": "no track.duplicate() method in the DSL (TE has DuplicateTrackCommand; not surfaced)",
    "remove_plugin":   "no fx.remove() method in the DSL",
    "route_track":     "no send/route method in the DSL (TrackManager.addSend exists; not surfaced)",
    "group_by_substr": "filter condition only supports '==' (exact); 'tracks containing bass' needs a 'contains' op or state-driven id resolution",
}

# ---------------------------------------------------------------------------
# Plugin inventory: display name + NL spellings -> DSL alias token.
# Internal FX use bare names (eq, reverb, ...); third-party use <alias> tokens.
# ---------------------------------------------------------------------------
# Third-party instruments + effects (emit <alias> tokens).
THIRD_PARTY = {
    "serum":      {"token": "<serum>",      "names": ["serum", "serum 2", "xfer serum"]},
    "ott":        {"token": "<ott>",        "names": ["ott", "xfer ott"]},
    "pro-q 3":    {"token": "<pro_q_3>",    "names": ["pro-q", "pro-q 3", "proq", "fabfilter pro-q", "fabfilter pro-q 3"]},
    "pro-c 2":    {"token": "<pro_c_2>",    "names": ["pro-c", "pro-c 2", "fabfilter pro-c"]},
    "pro-l 2":    {"token": "<pro_l_2>",    "names": ["pro-l", "pro-l 2", "fabfilter pro-l"]},
    "surge xt":   {"token": "<surge_xt>",   "names": ["surge", "surge xt"]},
    "vital":      {"token": "<vital>",      "names": ["vital"]},
    "diva":       {"token": "<diva>",       "names": ["diva", "u-he diva"]},
    "massive":    {"token": "<massive>",    "names": ["massive", "ni massive"]},
    "valhalla":   {"token": "<valhalla_vintage_verb>", "names": ["valhalla", "valhalla vintage verb", "vintageverb"]},
    "1176":       {"token": "<1176>",       "names": ["1176", "uad 1176"]},
    "decapitator":{"token": "<decapitator>","names": ["decapitator", "soundtoys decapitator"]},
}

# Internal MAGDA FX (emit bare lowercase names, per the tool description).
INTERNAL_FX = {
    "eq":          ["eq", "equalizer", "equaliser"],
    "compressor":  ["compressor", "comp"],
    "reverb":      ["reverb", "verb"],
    "delay":       ["delay", "echo"],
    "chorus":      ["chorus"],
    "phaser":      ["phaser"],
    "filter":      ["filter"],
    "utility":     ["utility", "gain"],
    "pitch shift": ["pitch shift", "pitch shifter"],
    "ir reverb":   ["ir reverb", "convolution reverb", "convolution"],
}


def resolve_plugin(text):
    """Map a free-text plugin mention to the DSL token to emit.

    Returns the alias token / internal name, or None if unrecognised.
    Longest-match-first so 'pro-q 3' wins over 'pro-q'.
    """
    t = text.strip().lower()
    # third-party aliases
    candidates = []
    for spec in THIRD_PARTY.values():
        for n in spec["names"]:
            candidates.append((n, spec["token"]))
    for name, names in INTERNAL_FX.items():
        for n in names:
            candidates.append((n, name))
    candidates.sort(key=lambda c: len(c[0]), reverse=True)
    for needle, token in candidates:
        if t == needle:
            return token
    # substring fallback (handles 'add a serum')
    for needle, token in candidates:
        if needle in t:
            return token
    return None


# ---------------------------------------------------------------------------
# Colour palette: colour word -> hex (matches the brand-ish values in the
# DSL tool-description examples, e.g. red = #ff5a36).
# ---------------------------------------------------------------------------
COLORS = {
    "red":     "#ff5a36",
    "orange":  "#ff8c42",
    "yellow":  "#ffd23f",
    "green":   "#3ddc84",
    "teal":    "#1abc9c",
    "blue":    "#44c7ff",
    "purple":  "#9b59b6",
    "pink":    "#ff6ad5",
    "grey":    "#8a8a8a",
    "gray":    "#8a8a8a",
}


def resolve_color(text):
    """colour word -> hex, or None."""
    return COLORS.get(text.strip().lower())
