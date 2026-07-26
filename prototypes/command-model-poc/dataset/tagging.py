"""Intent + slot-tag labels for the FPGA-friendly model (single forward pass).

Converts (input, actions) -> (intent, tokens, BIO tags). This is the supervised
target for a non-autoregressive model: one encoder pass produces an intent label
(which command) and a per-token slot tag (which word is the track name / plugin /
colour / id / group name). No decode loop -> streams cleanly on FPGA (FINN).

Crucially it round-trips: reconstruct(intent, tokens, tags) -> actions ->
dsl.render() must reproduce the gold DSL. If it does, the tags carry all the
information the DSL needs, and the neural net never has to emit DSL text - the
deterministic renderer (already built) does, on the CPU/ARM side.

    python -m dataset.tagging --demo                       # show tagged examples + round-trip
    python -m dataset.tagging --in data/train.jsonl --out data/train.tags.jsonl
"""
from __future__ import annotations

import argparse
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from magda_dsl import dsl, vocab  # noqa: E402

# Slot inventory (role-typed: rename needs TRACK_NAME + NEW_NAME distinctly).
SLOTS = [
    "TRACK_NAME", "NEW_NAME", "PLUGIN", "COLOR", "TRACK_ID", "GROUP_NAME",
    "VALUE", "CLIP_NAME", "CLIP_TYPE", "PITCH", "GROOVE_NAME",
    # Role-typed numerics. Both are separate from VALUE because a request can
    # carry two numbers at once ("a 4 bar clip at bar 49", "create 3 tracks"),
    # and a single generic VALUE cannot say which is which.
    "COUNT", "BAR",
]

# Number words, so "create three tracks" works as well as "create 3 tracks".
NUMBER_WORDS = {
    "one": 1, "two": 2, "three": 3, "four": 4, "five": 5, "six": 6,
    "seven": 7, "eight": 8, "nine": 9, "ten": 10, "a couple of": 2,
    "a couple": 2, "a few": 3,
}

# Note-duration grids: surface phrase -> grid value (16th=0.25, 8th=0.5, ...).
GRID_PHRASES = {
    0.125: ["32nd", "32nds"], 0.25: ["16th", "16ths"],
    0.5: ["8th", "8ths", "eighth", "eighths"], 1.0: ["quarter", "quarters"],
}

# Tokens keep a leading @ (plugin alias sigil), internal _ (alias tokens like
# @pro_q_3), and a trailing decimal (0.5, 1.0) so a whole alias/number stays
# one token.
_TOK = re.compile(r"[@#]?[A-Za-z0-9_'\-]+(?:\.[0-9]+)?")

# Units people type glued to the number ("-6db", "4bars"). Split those so the
# numeric value can be tagged, but ONLY for this whitelist: a general
# digits-then-letters split would wreck "16ths" (a grid phrase), "C3" (a pitch)
# and "@pro_q_3" (an alias).
_GLUED_UNIT = re.compile(r"^([+-]?[0-9]+(?:\.[0-9]+)?)(db|bars?|beats?|semitones?|st)$",
                         re.IGNORECASE)


# "track1" / "track12" — a track reference typed without a space. Split so the
# digits can be tagged as a TRACK_ID. Deliberately anchored on the literal word
# "track": a general letters-then-digits split would wreck "@fm_0", "pro_q_3"
# and "C3".
_GLUED_TRACK = re.compile(r"^(tracks?)([0-9]+)$", re.IGNORECASE)


def _split_glued_units(tokens):
    out = []
    for t in tokens:
        m = _GLUED_UNIT.match(t)
        if m:
            out.extend([m.group(1), m.group(2)])
            continue
        m = _GLUED_TRACK.match(t)
        out.extend([m.group(1), m.group(2)] if m else [t])
    return out


def _alias_surface(token: str) -> str:
    """DSL alias token '<serum>' -> the '@serum' surface the user types."""
    return "@" + token.strip("<>")


def _alias_token(surface: str) -> str:
    """'@serum' surface -> DSL alias token '<serum>'."""
    return "<" + surface.lstrip("@").strip("<>") + ">"

TRACK_NAME_HINTS = [
    "reese bass", "sub bass", "top loop", "bass", "drums", "lead", "pads",
    "vocals", "kick", "snare", "hats", "pluck", "arp", "strings", "brass",
    "guitar", "perc", "keys", "synth", "fx", "sub", "vox", "choir",
]


def tokenize(text: str):
    return _split_glued_units(_TOK.findall(text))


def _subseq(lower_tokens, words, tags=None):
    """First start index where `words` appears contiguously, else -1.

    When `tags` is given, occurrences whose tokens are already claimed by
    another slot are skipped. Two slots overlapping is always a bug: "a 1 bar
    clip at bar 1" would have the length claim the position's token and then
    overwrite it, losing the position entirely.
    """
    if not words:
        return -1
    for i in range(len(lower_tokens) - len(words) + 1):
        if lower_tokens[i:i + len(words)] != words:
            continue
        if tags is not None and any(t != "O" for t in tags[i:i + len(words)]):
            continue
        return i
    return -1


def _tag_span(tags, lower, value, slot):
    """Tag the first UNCLAIMED contiguous occurrence of `value` as B-/I-slot."""
    words = value.lower().split()
    i = _subseq(lower, words, tags)
    if i < 0:
        return False
    tags[i] = f"B-{slot}"
    for j in range(1, len(words)):
        tags[i + j] = f"I-{slot}"
    return True


# ---------------------------------------------------------------------------
# (input, actions) -> (intent, tokens, tags)
# ---------------------------------------------------------------------------
def tag(input_text: str, actions: list[dict]):
    tokens = tokenize(input_text)
    lower = [t.lower() for t in tokens]
    tags = ["O"] * len(tokens)
    a = actions[0]
    intent = a["type"]

    if intent in ("delete_track", "select_all_clips"):
        _tag_span(tags, lower, a["name"], "TRACK_NAME")

    elif intent == "select_all_clips_rename":
        _tag_span(tags, lower, a["new_name"], "NEW_NAME")
        _tag_span(tags, lower, a["name"], "TRACK_NAME")

    elif intent in ("select_clips_named", "select_clips_not_named"):
        _tag_span(tags, lower, a["name"], "TRACK_NAME")
        _tag_span(tags, lower, a["clip_name"], "CLIP_NAME")

    elif intent == "select_clips_type":
        _tag_span(tags, lower, a["name"], "TRACK_NAME")
        _tag_span(tags, lower, a["clip_type"], "CLIP_TYPE")

    elif intent in ("select_clips_longer_than", "select_clips_shorter_than",
                    "select_clips_length_at_least", "select_clips_length_at_most",
                    "select_clips_length_exactly"):
        _tag_span(tags, lower, a["name"], "TRACK_NAME")
        _tag_value(tags, lower, a["bars"])

    elif intent in ("select_clips_starting_after", "select_clips_starting_before"):
        _tag_span(tags, lower, a["name"], "TRACK_NAME")
        _tag_value(tags, lower, a["bar"])

    elif intent in ("create_track", "create_rack", "create_rack_parallel"):
        # A repeat count ("create 3 bass tracks") is tagged before the name so
        # a numeric name cannot swallow it.
        if len(actions) > 1 and intent == "create_track":
            n = len(actions)
            if not _tag_span(tags, lower, str(n), "COUNT"):
                for word, val in NUMBER_WORDS.items():
                    if val == n and _tag_span(tags, lower, word, "COUNT"):
                        break
        _tag_span(tags, lower, a["name"], "TRACK_NAME")
        for tok in a.get("plugins", []):
            _tag_span(tags, lower, _alias_surface(tok), "PLUGIN")

    elif intent == "add_plugin":
        _tag_span(tags, lower, a["name"], "TRACK_NAME")
        for act in actions:  # add_plugin may fan out over several plugins
            _tag_span(tags, lower, _alias_surface(act["plugin"]), "PLUGIN")

    elif intent == "rename_track":
        _tag_span(tags, lower, a["new_name"], "NEW_NAME")  # tag NEW first (often distinct)
        _tag_span(tags, lower, a["name"], "TRACK_NAME")

    elif intent == "set_track_color":
        _tag_span(tags, lower, a["name"], "TRACK_NAME")
        for word, hexv in vocab.COLORS.items():
            if hexv == a["colour"] and _tag_span(tags, lower, word, "COLOR"):
                break

    elif intent == "set_track_volume":
        _tag_span(tags, lower, a["name"], "TRACK_NAME")
        _tag_value(tags, lower, a["volume_db"])

    elif intent == "set_track_pan":
        _tag_span(tags, lower, a["name"], "TRACK_NAME")
        if not _tag_pan_phrase(tags, lower, a["pan"]):
            _tag_value(tags, lower, a["pan"])

    elif intent == "select_tracks":
        ids = [act["id"] for act in actions]
        for i, t in enumerate(lower):
            if t.isdigit() and int(t) in ids:
                tags[i] = "B-TRACK_ID"

    elif intent == "group_tracks":
        for i, t in enumerate(lower):
            if t.isdigit() and int(t) in a["ids"]:
                tags[i] = "B-TRACK_ID"
        _tag_span(tags, lower, a["name"], "GROUP_NAME")

    elif intent == "clip_new":
        _tag_span(tags, lower, a["name"], "TRACK_NAME")
        # Tag the bar FIRST: "a clip at bar 49" has only one number, and it is
        # the position, not the length. Tagging length first would claim it.
        # Anchor on the preposition, because "a 1 bar clip at bar 1" repeats the
        # same digit and a plain first-occurrence search tags the length.
        if a.get("bar") is not None:
            _tag_bar(tags, lower, a["bar"])
        _tag_value(tags, lower, a["length_bars"])

    elif intent == "clip_mute":
        _tag_span(tags, lower, a["name"], "TRACK_NAME")
        if a.get("clip_name"):
            _tag_span(tags, lower, a["clip_name"], "CLIP_NAME")
        if a.get("index") is not None:
            _tag_value(tags, lower, a["index"])

    elif intent == "clip_rename":
        _tag_span(tags, lower, a["name"], "TRACK_NAME")
        _tag_span(tags, lower, a["clip_name"], "CLIP_NAME")

    elif intent == "clip_delete":
        _tag_span(tags, lower, a["name"], "TRACK_NAME")
        _tag_value(tags, lower, a["index"])

    elif intent == "track_move":
        _tag_span(tags, lower, a["name"], "TRACK_NAME")
        _tag_value(tags, lower, a["index"])

    elif intent == "notes_delete":
        _tag_span(tags, lower, a["name"], "TRACK_NAME")

    elif intent == "notes_transpose":
        _tag_span(tags, lower, a["name"], "TRACK_NAME")
        _tag_value(tags, lower, abs(a["semitones"]))  # sign carried by up/down word

    elif intent in ("notes_set_velocity", "notes_select_velocity_above",
                    "notes_select_velocity_below"):
        _tag_span(tags, lower, a["name"], "TRACK_NAME")
        _tag_value(tags, lower, a["value"])

    elif intent == "notes_resize":
        _tag_span(tags, lower, a["name"], "TRACK_NAME")
        _tag_value(tags, lower, a["length"])

    elif intent == "notes_quantize":
        _tag_span(tags, lower, a["name"], "TRACK_NAME")
        if not _tag_grid_phrase(tags, lower, a["grid"]):
            _tag_value(tags, lower, a["grid"])

    elif intent in ("notes_set_pitch", "notes_select_pitch"):
        _tag_span(tags, lower, a["name"], "TRACK_NAME")
        _tag_span(tags, lower, a["pitch"], "PITCH")

    elif intent == "groove_set":
        _tag_span(tags, lower, a["template"], "GROOVE_NAME")
        _tag_value(tags, lower, a["strength"])

    elif intent == "unsupported":
        pass

    return intent, tokens, tags


def _surface_candidates(token: str):
    """Plugin token (e.g. '<serum>' or 'eq') -> possible surface spellings."""
    for spec in vocab.THIRD_PARTY.values():
        if spec["token"] == token:
            return sorted(spec["names"], key=len, reverse=True)
    for name, names in vocab.INTERNAL_FX.items():
        if name == token:
            return sorted(names, key=len, reverse=True)
    return [token]


def _tag_value(tags, lower, value):
    """Tag a numeric value, tolerating int/float canonical forms."""
    candidates = [f"{float(value):g}", str(value)]
    if float(value) > 0:
        candidates.append("+" + f"{float(value):g}")
    for c in dict.fromkeys(candidates):
        if _tag_span(tags, lower, c, "VALUE"):
            return True
    return False


def _tag_bar(tags, lower, bar) -> bool:
    """Tag a bar POSITION, anchored on the "at bar N" / "bar N" preposition.

    Position and length can be the same number ("a 1 bar clip at bar 1"), and a
    first-occurrence search would tag the length as the position and then have
    it overwritten — losing the bar entirely.
    """
    target = f"{float(bar):g}"
    for i, tok in enumerate(lower):
        if tok != target:
            continue
        prev = lower[i - 1] if i else ""
        prev2 = lower[i - 2] if i >= 2 else ""
        # "at bar N" / "starting at bar N" / "at N"
        if prev == "bar" and prev2 in ("at", "from", "starting"):
            tags[i] = "B-BAR"
            return True
        if prev in ("at", "from"):
            tags[i] = "B-BAR"
            return True
    # Fall back to the last "bar N" in the text; in "4 bar clip at bar 9" the
    # length reads "N bar" (number BEFORE) and the position "bar N" (after).
    for i in range(len(lower) - 1, 0, -1):
        if lower[i] == target and lower[i - 1] == "bar":
            tags[i] = "B-BAR"
            return True
    return False


def _tag_grid_phrase(tags, lower, grid):
    """Tag a note-duration phrase ('16th', '8th', ...) as the VALUE span."""
    for phrase in GRID_PHRASES.get(float(grid), []):
        if _tag_span(tags, lower, phrase, "VALUE"):
            return True
    return False


def _tag_pan_phrase(tags, lower, pan):
    phrases = {
        -1.0: ["hard left"],
        -0.5: ["left"],
        -0.25: ["slightly left"],
        0.0: ["center", "centre", "middle"],
        0.25: ["slightly right"],
        0.5: ["right"],
        1.0: ["hard right"],
    }
    for phrase in phrases.get(float(pan), []):
        if _tag_span(tags, lower, phrase, "VALUE"):
            return True
    return False


# ---------------------------------------------------------------------------
# (intent, tokens, tags) -> actions   (proves the tags are lossless)
# ---------------------------------------------------------------------------
def _spans(tokens, tags):
    """Collect {slot: [text, ...]} from BIO tags."""
    out = {}
    cur_slot, cur = None, []
    for tok, tg in zip(tokens, tags):
        if tg.startswith("B-"):
            if cur_slot:
                out.setdefault(cur_slot, []).append(" ".join(cur))
            cur_slot, cur = tg[2:], [tok]
        elif tg.startswith("I-") and cur_slot == tg[2:]:
            cur.append(tok)
        else:
            if cur_slot:
                out.setdefault(cur_slot, []).append(" ".join(cur))
            cur_slot, cur = None, []
    if cur_slot:
        out.setdefault(cur_slot, []).append(" ".join(cur))
    return out


def _canon_name(s: str) -> str:
    return " ".join(w.capitalize() if w.islower() else w for w in s.split())


def reconstruct(intent: str, tokens, tags) -> list[dict]:
    s = _spans(tokens, tags)
    name = _canon_name(s.get("TRACK_NAME", [""])[0]) if s.get("TRACK_NAME") else ""
    if not name:
        # Only guess from bare tokens that no other slot claimed — otherwise
        # "skip the Lead Take clip" reads "Lead" out of the CLIP name.
        claimed = {w.lower() for slot_vals in s.values() for v in slot_vals
                   for w in v.split()}
        name = _track_name_from_tokens([t for t in tokens if t.lower() not in claimed])
    intent = _refine_intent(intent, tokens, name)

    if intent in ("create_track", "create_rack", "create_rack_parallel"):
        act = {"type": intent, "name": name}
        plugs = [_alias_token(p) for p in s.get("PLUGIN", [])]
        if not plugs:
            plugs = _plugins_from_tokens(tokens)
        if plugs:
            act["plugins"] = plugs
        # "create 3 tracks" -> the same action repeated. The model emits one
        # intent; the count is a slot, and the repetition happens here.
        if intent == "create_track" and s.get("COUNT"):
            n = _parse_count(s["COUNT"][0])
            if n > 1:
                return [dict(act) for _ in range(n)]
        return [act]
    if intent == "add_plugin":
        plugs = [_alias_token(p) for p in s.get("PLUGIN", [])]
        if not plugs:
            plugs = _plugins_from_tokens(tokens)
        if not plugs:
            return [{"type": "add_plugin", "name": name, "plugin": None}]
        # Fan out: one add_plugin action per plugin, like create_track.
        return [{"type": "add_plugin", "name": name, "plugin": p} for p in plugs]
    if intent == "rename_track":
        return [{"type": "rename_track", "name": name,
                 "new_name": _canon_name(s.get("NEW_NAME", [""])[0])}]
    if intent == "delete_track":
        return [{"type": intent, "name": name}]
    if intent == "select_all_clips":
        return [{"type": "select_all_clips", "name": name}]
    if intent == "select_all_clips_rename":
        new_name = _canon_name(s.get("NEW_NAME", [""])[0]) if s.get("NEW_NAME") else ""
        if not new_name:
            new_name = _rename_target_from_tokens(tokens)
        return [{"type": "select_all_clips_rename", "name": name,
                 "new_name": new_name}]
    if intent in ("select_clips_named", "select_clips_not_named"):
        return [{"type": intent, "name": name,
                 "clip_name": _canon_name(s.get("CLIP_NAME", [""])[0])}]
    if intent == "select_clips_type":
        return [{"type": "select_clips_type", "name": name,
                 "clip_type": s.get("CLIP_TYPE", [""])[0].lower()}]
    if intent in ("select_clips_longer_than", "select_clips_shorter_than",
                  "select_clips_length_at_least", "select_clips_length_at_most",
                  "select_clips_length_exactly"):
        return [{"type": intent, "name": name,
                 "bars": _value_or_first_number(s, tokens)}]
    if intent in ("select_clips_starting_after", "select_clips_starting_before"):
        return [{"type": intent, "name": name,
                 "bar": _value_or_first_number(s, tokens)}]
    if intent == "set_track_color":
        colour = None
        for w in s.get("COLOR", []):
            colour = vocab.resolve_color(w) or colour
        return [{"type": "set_track_color", "name": name, "colour": colour}]
    if intent == "set_track_volume":
        return [{"type": "set_track_volume", "name": name,
                 "volume_db": _parse_number(s.get("VALUE", ["0"])[0])}]
    if intent == "set_track_pan":
        return [{"type": "set_track_pan", "name": name,
                 "pan": _parse_pan(s.get("VALUE", ["0"])[0])}]
    if intent == "select_tracks":
        ids = [int(x) for x in s.get("TRACK_ID", [])]
        return [{"type": "select_tracks", "id": i} for i in ids] or \
            [{"type": "select_tracks", "id": 1}]
    if intent == "group_tracks":
        ids = [int(x) for x in s.get("TRACK_ID", [])]
        # A name is optional: "group track 1 and track 2" is a complete
        # request, and the interpreter is happy with a default label.
        gname = _canon_name(s.get("GROUP_NAME", [""])[0]) if s.get("GROUP_NAME") else "Group"
        return [{"type": "group_tracks", "ids": ids, "name": gname or "Group"}]
    if intent == "clip_new":
        bar = _parse_number(s["BAR"][0]) if s.get("BAR") else None
        # With only a bar given, length falls back to the interpreter's own
        # default rather than reusing the bar as a length — which is what made
        # "create a clip at bar 49" produce a 49-bar clip.
        length = _parse_number(s["VALUE"][0]) if s.get("VALUE") else (
            4.0 if bar is not None else _value_or_first_number(s, tokens))
        act = {"type": "clip_new", "name": name, "length_bars": length}
        if bar is not None:
            act["bar"] = bar
        return [act]
    if intent == "clip_mute":
        # Default is to disable; "unmute"/"enable"/"back on" flip it. Testing
        # for a bare "on" is wrong — "turn off clip 0 ON Foley" contains one.
        low = [t.lower() for t in tokens]
        text = " ".join(low)
        enabled = (any(w in low for w in ("unmute", "enable", "enabled", "re-enable"))
                   or "back on" in text)
        if any(w in low for w in ("mute", "disable", "silence", "skip", "off")):
            enabled = False
        act = {"type": "clip_mute", "name": name, "enabled": enabled}
        if s.get("CLIP_NAME"):
            act["clip_name"] = _canon_name(s["CLIP_NAME"][0])
        elif s.get("VALUE"):
            act["index"] = _parse_number(s["VALUE"][0])
        return [act]
    if intent == "clip_rename":
        return [{"type": "clip_rename", "name": name,
                 "clip_name": _canon_name(s.get("CLIP_NAME", [""])[0])}]
    if intent == "clip_delete":
        return [{"type": "clip_delete", "name": name,
                 "index": _value_or_first_number(s, tokens)}]
    if intent == "track_move":
        return [{"type": "track_move", "name": name,
                 "index": _value_or_first_number(s, tokens)}]
    if intent == "notes_delete":
        return [{"type": "notes_delete", "name": name}]
    if intent == "notes_transpose":
        n = abs(_value_or_first_number(s, tokens))
        low = [t.lower() for t in tokens]
        # "drop"/"dropped" read as downward here; the templates only ever said
        # "down"/"lower", so anything else came back +N (#1847 OOD miss).
        if any(w in low for w in ("down", "lower", "drop", "dropped", "below")):
            n = -n
        return [{"type": "notes_transpose", "name": name, "semitones": n}]
    if intent == "notes_set_velocity":
        return [{"type": "notes_set_velocity", "name": name,
                 "value": _value_or_first_number(s, tokens)}]
    if intent in ("notes_select_velocity_above", "notes_select_velocity_below"):
        return [{"type": intent, "name": name,
                 "value": _value_or_first_number(s, tokens)}]
    if intent == "notes_resize":
        return [{"type": "notes_resize", "name": name,
                 "length": _value_or_first_number(s, tokens)}]
    if intent == "notes_quantize":
        grid = _parse_grid(s["VALUE"][0]) if s.get("VALUE") else 0.25
        return [{"type": "notes_quantize", "name": name, "grid": grid}]
    if intent in ("notes_set_pitch", "notes_select_pitch"):
        return [{"type": intent, "name": name,
                 "pitch": _canon_pitch(s.get("PITCH", ["C4"])[0])}]
    if intent == "groove_set":
        return [{"type": "groove_set",
                 "template": _canon_name(s.get("GROOVE_NAME", [""])[0]),
                 "strength": _value_or_first_number(s, tokens)}]
    if intent == "unsupported":
        return [{"type": "unsupported"}]
    raise ValueError(intent)


def _parse_grid(text: str) -> float:
    for grid, phrases in GRID_PHRASES.items():
        if text.lower().strip() in phrases:
            return grid
    return _parse_number(text) or 0.25


def _canon_pitch(text: str) -> str:
    t = text.strip()
    if re.fullmatch(r"\d+", t):          # MIDI number
        return t
    m = re.fullmatch(r"([a-gA-G])([b]?)(\d)", t)
    if not m:
        return t.upper()
    return m.group(1).upper() + m.group(2).lower() + m.group(3)


def _parse_count(text: str) -> int:
    """COUNT span -> repetition count. Accepts digits and number words, and is
    capped: "create 500 tracks" is far more likely a typo than an intention."""
    t = text.lower().strip()
    if t in NUMBER_WORDS:
        return min(NUMBER_WORDS[t], 16)
    m = re.search(r"\d+", t)
    return min(int(m.group(0)), 16) if m else 1


def _parse_number(text: str) -> float:
    m = re.search(r"[+-]?\d+(?:\.\d+)?", text)
    return float(m.group(0)) if m else 0.0


def _value_or_first_number(spans, tokens) -> float:
    if spans.get("VALUE"):
        return _parse_number(spans["VALUE"][0])
    return _parse_number(" ".join(tokens))


# Verbs that unambiguously mean "remove this track". Deliberately broad, since
# the guard below only *blocks* on their absence.
_DELETE_VERBS = ("delete", "remove", "bin", "trash", "scrap", "kill", "ditch",
                 "lose", "rid", "drop")


def _refine_intent(intent: str, tokens, name: str = "") -> str:
    """Guard the one intent whose errors are unrecoverable.

    delete_track destroys work, so it is the one place worth being deterministic
    rather than trusting the classifier. Without a resolved track name it needs
    both a deletion verb AND a mention of a track; otherwise nothing in the
    sentence actually asked to delete anything in the project.

    "undo that" has neither and used to render track(name="").delete().
    "scrap that" has the verb but no object — "that" is the previous action, not
    a track. "delete the selected track" has no name span but says "track", so
    it passes.
    """
    if intent != "delete_track":
        return intent
    if name:
        return "delete_track"          # an explicit target settles it
    text = " ".join(tokens).lower()
    has_verb = any(re.search(r"\b" + v + r"\b", text) for v in _DELETE_VERBS)
    names_a_track = re.search(r"\btracks?\b", text) is not None
    # A verb alone is not enough: "scrap that" and "delete that" carry one but
    # point at no track — "that" is the previous action, not an object in the
    # project. Require something track-shaped to act on.
    if has_verb and names_a_track:
        return "delete_track"
    return "unsupported"


def _track_name_from_tokens(tokens) -> str:
    text = " ".join(tokens).lower()
    for name in sorted(TRACK_NAME_HINTS, key=len, reverse=True):
        if re.search(r"\b" + re.escape(name) + r"\b", text):
            return _canon_name(name)
    return ""


def _rename_target_from_tokens(tokens) -> str:
    lower = [t.lower() for t in tokens]
    for phrase in (["rename", "them", "to"], ["rename", "to"], ["call", "them"], ["to"]):
        i = _subseq(lower, phrase)
        if i >= 0:
            tail = tokens[i + len(phrase):]
            if tail:
                return _canon_name(" ".join(tail))
    return ""


def _parse_pan(text: str) -> float:
    t = text.lower().strip()
    words = {
        "hard left": -1.0,
        "left": -0.5,
        "slightly left": -0.25,
        "center": 0.0,
        "centre": 0.0,
        "middle": 0.0,
        "slightly right": 0.25,
        "right": 0.5,
        "hard right": 1.0,
    }
    if t in words:
        return words[t]
    return _parse_number(t)


def _plugins_from_tokens(tokens) -> list[str]:
    """Fallback when slot tags miss: any @alias token is a plugin reference.
    Generic by design — the model needn't know plugin names; the alias system
    injects them and resolves them at runtime."""
    out = []
    for t in tokens:
        if t.startswith("@"):
            tok = _alias_token(t)
            if tok not in out:
                out.append(tok)
    return out


def roundtrip_ok(row) -> bool:
    intent, tokens, tags = tag(row["input"], row["actions"])
    try:
        rebuilt = dsl.render(reconstruct(intent, tokens, tags))
    except Exception:
        return False
    return dsl.normalize(rebuilt) == dsl.normalize(row["output"])


def main():
    here = os.path.dirname(__file__)
    ap = argparse.ArgumentParser()
    ap.add_argument("--in", dest="inp", default=os.path.join(here, "..", "data", "train.jsonl"))
    ap.add_argument("--out", dest="out")
    ap.add_argument("--demo", action="store_true")
    args = ap.parse_args()

    rows = [json.loads(l) for l in open(args.inp, encoding="utf-8") if l.strip()]

    if args.demo:
        seen = set()
        for row in rows:
            it = row["actions"][0]["type"]
            if it in seen:
                continue
            seen.add(it)
            intent, tokens, tags = tag(row["input"], row["actions"])
            print(f"\nINTENT: {intent}")
            for tk, tg in zip(tokens, tags):
                print(f"  {tk:<12} {tg}")
            rebuilt = dsl.render(reconstruct(intent, tokens, tags))
            ok = dsl.normalize(rebuilt) == dsl.normalize(row["output"])
            print(f"  -> render: {rebuilt!r}  [{'OK' if ok else 'MISMATCH gold=' + row['output']!r}]")

    ok = sum(roundtrip_ok(r) for r in rows)
    print(f"\nround-trip (tags -> render == gold): {ok}/{len(rows)} = {ok/len(rows):.1%}")

    if args.out:
        with open(args.out, "w", encoding="utf-8") as f:
            for row in rows:
                intent, tokens, tags = tag(row["input"], row["actions"])
                f.write(json.dumps({"intent": intent, "tokens": tokens, "tags": tags,
                                    "input": row["input"]}, ensure_ascii=False) + "\n")
        print(f"wrote tags -> {os.path.abspath(args.out)}")


if __name__ == "__main__":
    main()
