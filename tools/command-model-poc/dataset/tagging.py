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
    "VALUE", "CLIP_NAME", "CLIP_TYPE",
]

_TOK = re.compile(r"#?[A-Za-z0-9'\-]+")

TRACK_NAME_HINTS = [
    "reese bass", "sub bass", "top loop", "bass", "drums", "lead", "pads",
    "vocals", "kick", "snare", "hats", "pluck", "arp", "strings", "brass",
    "guitar", "perc", "keys", "synth", "fx", "sub", "vox", "choir",
]


def tokenize(text: str):
    return _TOK.findall(text)


def _subseq(lower_tokens, words):
    """First start index where `words` appears contiguously, else -1."""
    if not words:
        return -1
    for i in range(len(lower_tokens) - len(words) + 1):
        if lower_tokens[i:i + len(words)] == words:
            return i
    return -1


def _tag_span(tags, lower, value, slot):
    """Tag the first contiguous occurrence of `value` (string) as B-/I-slot."""
    words = value.lower().split()
    i = _subseq(lower, words)
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

    if intent in ("delete_track", "mute_track", "solo_track", "select_all_clips"):
        _tag_span(tags, lower, a["name"], "TRACK_NAME")

    elif intent == "select_all_clips_rename":
        _tag_span(tags, lower, a["new_name"], "NEW_NAME")
        _tag_span(tags, lower, a["name"], "TRACK_NAME")

    elif intent == "select_clips_named":
        _tag_span(tags, lower, a["name"], "TRACK_NAME")
        _tag_span(tags, lower, a["clip_name"], "CLIP_NAME")

    elif intent == "select_clips_type":
        _tag_span(tags, lower, a["name"], "TRACK_NAME")
        _tag_span(tags, lower, a["clip_type"], "CLIP_TYPE")

    elif intent in ("select_clips_longer_than", "select_clips_shorter_than"):
        _tag_span(tags, lower, a["name"], "TRACK_NAME")
        _tag_value(tags, lower, a["bars"])

    elif intent in ("select_clips_starting_after", "select_clips_starting_before"):
        _tag_span(tags, lower, a["name"], "TRACK_NAME")
        _tag_value(tags, lower, a["bar"])

    elif intent == "create_track":
        _tag_span(tags, lower, a["name"], "TRACK_NAME")
        for tok in a.get("plugins", []):
            for name in _surface_candidates(tok):
                if _tag_span(tags, lower, name, "PLUGIN"):
                    break

    elif intent == "add_plugin":
        _tag_span(tags, lower, a["name"], "TRACK_NAME")
        for name in _surface_candidates(a["plugin"]):
            if _tag_span(tags, lower, name, "PLUGIN"):
                break

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

    elif intent == "group_tracks":
        for i, t in enumerate(lower):
            if t.isdigit() and int(t) in a["ids"]:
                tags[i] = "B-TRACK_ID"
        _tag_span(tags, lower, a["name"], "GROUP_NAME")

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
    intent = _refine_intent(intent, tokens)
    s = _spans(tokens, tags)
    name = _canon_name(s.get("TRACK_NAME", [""])[0]) if s.get("TRACK_NAME") else ""
    if not name:
        name = _track_name_from_tokens(tokens)

    if intent == "create_track":
        act = {"type": "create_track", "name": name}
        plugs = [vocab.resolve_plugin(p) for p in s.get("PLUGIN", [])]
        plugs = [p for p in plugs if p]
        if not plugs:
            plugs = _plugins_from_tokens(tokens)
        if plugs:
            act["plugins"] = plugs
        return [act]
    if intent == "add_plugin":
        plug = vocab.resolve_plugin(s.get("PLUGIN", [""])[0]) if s.get("PLUGIN") else None
        if not plug:
            plugs = _plugins_from_tokens(tokens)
            plug = plugs[0] if plugs else None
        return [{"type": "add_plugin", "name": name, "plugin": plug}]
    if intent == "rename_track":
        return [{"type": "rename_track", "name": name,
                 "new_name": _canon_name(s.get("NEW_NAME", [""])[0])}]
    if intent in ("delete_track", "mute_track", "solo_track"):
        return [{"type": intent, "name": name}]
    if intent == "select_all_clips":
        return [{"type": "select_all_clips", "name": name}]
    if intent == "select_all_clips_rename":
        new_name = _canon_name(s.get("NEW_NAME", [""])[0]) if s.get("NEW_NAME") else ""
        if not new_name:
            new_name = _rename_target_from_tokens(tokens)
        return [{"type": "select_all_clips_rename", "name": name,
                 "new_name": new_name}]
    if intent == "select_clips_named":
        return [{"type": "select_clips_named", "name": name,
                 "clip_name": _canon_name(s.get("CLIP_NAME", [""])[0])}]
    if intent == "select_clips_type":
        return [{"type": "select_clips_type", "name": name,
                 "clip_type": s.get("CLIP_TYPE", [""])[0].lower()}]
    if intent in ("select_clips_longer_than", "select_clips_shorter_than"):
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
    if intent == "group_tracks":
        ids = [int(x) for x in s.get("TRACK_ID", [])]
        return [{"type": "group_tracks", "ids": ids,
                 "name": _canon_name(s.get("GROUP_NAME", [""])[0])}]
    raise ValueError(intent)


def _parse_number(text: str) -> float:
    m = re.search(r"[+-]?\d+(?:\.\d+)?", text)
    return float(m.group(0)) if m else 0.0


def _value_or_first_number(spans, tokens) -> float:
    if spans.get("VALUE"):
        return _parse_number(spans["VALUE"][0])
    return _parse_number(" ".join(tokens))


def _refine_intent(intent: str, tokens) -> str:
    if intent not in ("delete_track", "mute_track", "solo_track"):
        return intent
    text = " ".join(tokens).lower()
    if re.search(r"\b(delete|remove)\b", text):
        return "delete_track"
    if re.search(r"\b(mute|silence)\b", text):
        return "mute_track"
    if re.search(r"\b(solo|isolate)\b", text):
        return "solo_track"
    return intent


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
    """Fallback plugin lookup over the whole utterance when slot tags miss."""
    text = " ".join(tokens).lower()
    candidates = []
    for spec in vocab.THIRD_PARTY.values():
        for name in spec["names"]:
            candidates.append((name, spec["token"]))
    for token, names in vocab.INTERNAL_FX.items():
        for name in names:
            candidates.append((name, token))
    candidates.sort(key=lambda c: len(c[0]), reverse=True)

    found = []
    used = []
    for needle, token in candidates:
        for match in re.finditer(r"\b" + re.escape(needle) + r"\b", text):
            span = match.span()
            if any(not (span[1] <= s or span[0] >= e) for s, e in used):
                continue
            used.append(span)
            found.append((span[0], token))
    found.sort()

    out = []
    for _, token in found:
        if token not in out:
            out.append(token)
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
