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
SLOTS = ["TRACK_NAME", "NEW_NAME", "PLUGIN", "COLOR", "TRACK_ID", "GROUP_NAME"]

_TOK = re.compile(r"#?[A-Za-z0-9'\-]+")


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

    if intent in ("delete_track", "mute_track", "solo_track"):
        _tag_span(tags, lower, a["name"], "TRACK_NAME")

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

    if intent == "create_track":
        act = {"type": "create_track", "name": name}
        plugs = [vocab.resolve_plugin(p) for p in s.get("PLUGIN", [])]
        plugs = [p for p in plugs if p]
        if plugs:
            act["plugins"] = plugs
        return [act]
    if intent == "add_plugin":
        plug = vocab.resolve_plugin(s.get("PLUGIN", [""])[0]) if s.get("PLUGIN") else None
        return [{"type": "add_plugin", "name": name, "plugin": plug}]
    if intent == "rename_track":
        return [{"type": "rename_track", "name": name,
                 "new_name": _canon_name(s.get("NEW_NAME", [""])[0])}]
    if intent in ("delete_track", "mute_track", "solo_track"):
        return [{"type": intent, "name": name}]
    if intent == "set_track_color":
        colour = None
        for w in s.get("COLOR", []):
            colour = vocab.resolve_color(w) or colour
        return [{"type": "set_track_color", "name": name, "colour": colour}]
    if intent == "group_tracks":
        ids = [int(x) for x in s.get("TRACK_ID", [])]
        return [{"type": "group_tracks", "ids": ids,
                 "name": _canon_name(s.get("GROUP_NAME", [""])[0])}]
    raise ValueError(intent)


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
