"""Phase 1 baseline: rule/template NL -> MAGDA DSL parser (no model).

Purpose per the POC spec: establish the schema, exercise the eval harness, and
give a no-model accuracy floor that any trained model must beat. Deliberately
simple regex/keyword heuristics. `parse(text)` returns canonical DSL (via the
shared renderer) or "" when nothing matches.
"""
from __future__ import annotations

import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from magda_dsl import dsl, vocab  # noqa: E402

_NOISE = re.compile(r"\b(track|the|a|an|please|to|can you|could you)\b", re.I)
_LEAD_ARTICLE = re.compile(r"^(?:a|an|the|new)\s+", re.I)


def _strip_articles(s: str) -> str:
    prev = None
    s = s.strip()
    while prev != s:
        prev = s
        s = _LEAD_ARTICLE.sub("", s).strip()
    return s


def _clean_name(s: str) -> str:
    s = _strip_articles(s.strip().strip(".!?,").strip())
    # Title-case single words, preserve multiword casing loosely.
    return " ".join(w.capitalize() if w.islower() else w for w in s.split())


def _find_plugins(text: str):
    """Return list of DSL tokens for plugin mentions, in order of appearance."""
    found = []
    lt = text.lower()
    # Build (needle, token) sorted longest-first to avoid partial shadowing.
    candidates = []
    for spec in vocab.THIRD_PARTY.values():
        for n in spec["names"]:
            candidates.append((n, spec["token"]))
    for name, names in vocab.INTERNAL_FX.items():
        for n in names:
            candidates.append((n, name))
    candidates.sort(key=lambda c: len(c[0]), reverse=True)
    used_spans = []
    for needle, token in candidates:
        for m in re.finditer(r"\b" + re.escape(needle) + r"\b", lt):
            span = (m.start(), m.end())
            if any(not (span[1] <= s or span[0] >= e) for s, e in used_spans):
                continue
            used_spans.append(span)
            found.append((m.start(), token))
    found.sort()
    # dedupe tokens preserving order
    out = []
    for _, tok in found:
        if tok not in out:
            out.append(tok)
    return out


# --- matchers: each returns actions list or None ---------------------------

def _m_rename(text):
    m = re.search(r"\b(?:rename|change)\b.*?\b([\w ]+?)\s+to\s+([\w ]+)$", text, re.I)
    if not m:
        return None
    old = _clean_name(_NOISE.sub("", m.group(1)))
    new = _clean_name(m.group(2))
    if not old or not new:
        return None
    return [{"type": "rename_track", "name": old, "new_name": new}]


def _m_group(text):
    if "group" not in text.lower():
        return None
    ids = [int(x) for x in re.findall(r"\b(\d+)\b", text)]
    m = re.search(r"\b(?:as|into|called)\s+([\w ]+)$", text, re.I)
    if len(ids) < 2 or not m:
        return None
    return [{"type": "group_tracks", "ids": ids, "name": _clean_name(m.group(1))}]


def _m_create_with_plugins(text):
    if not re.search(r"\b(create|make|add|new)\b", text, re.I):
        return None
    if not re.search(r"\bwith\b|\band add\b", text, re.I):
        return None
    # name sits between create/make and 'track'
    m = re.search(r"\b(?:create|make|add|new)\b\s+(?:a |an |the |new )*([\w ]+?)\s+track\b", text, re.I)
    if not m:
        return None
    name = _clean_name(m.group(1))
    plugins = _find_plugins(text)
    if not name or not plugins:
        return None
    return [{"type": "create_track", "name": name, "plugins": plugins}]


def _m_create(text):
    # explicit "track called/named X" first (unambiguous)
    m = re.search(r"\btrack\b\s+(?:called|named)\s+([\w ]+)$", text, re.I)
    if m:
        name = _clean_name(m.group(1))
        return [{"type": "create_track", "name": name}] if name else None
    m = re.search(r"\b(?:create|make|add|new)\b\s+(?:a |an |the |new )*([\w ]+?)\s+track\b", text, re.I)
    if not m:
        return None
    name = _clean_name(m.group(1))
    if not name:
        return None
    return [{"type": "create_track", "name": name}]


def _m_add_plugin(text):
    m = re.search(r"\b(?:add|put|load)\b\s+(?:a |an )?(.+?)\s+(?:to|on|onto)\s+(?:the )?([\w ]+?)(?:\s+track)?$", text, re.I)
    if not m:
        return None
    plugins = _find_plugins(m.group(1))
    name = _clean_name(_NOISE.sub("", m.group(2)))
    if not plugins or not name:
        return None
    # multiple plugins -> one add_plugin action each
    return [{"type": "add_plugin", "name": name, "plugin": p} for p in plugins]


def _m_delete(text):
    m = re.search(r"\b(?:delete|remove)\b\s+(?:the )?([\w ]+?)(?:\s+track)?$", text, re.I)
    if not m:
        return None
    name = _clean_name(_NOISE.sub("", m.group(1)))
    if not name:
        return None
    return [{"type": "delete_track", "name": name}]


def _m_mute(text):
    m = re.search(r"\b(?:mute|silence)\b\s+(?:the )?([\w ]+?)(?:\s+track)?$", text, re.I)
    if not m:
        return None
    return [{"type": "mute_track", "name": _clean_name(_NOISE.sub("", m.group(1)))}]


def _m_solo(text):
    m = re.search(r"\b(?:solo|isolate)\b\s+(?:the )?([\w ]+?)(?:\s+track)?$", text, re.I)
    if not m:
        return None
    return [{"type": "solo_track", "name": _clean_name(_NOISE.sub("", m.group(1)))}]


def _m_color(text):
    cw = None
    for c in vocab.COLORS:
        if re.search(r"\b" + c + r"\b", text, re.I):
            cw = c
            break
    if not cw:
        return None
    if not re.search(r"\b(colou?r|make|set|code)\b", text, re.I):
        return None
    # name = words before the colour, minus verbs/noise
    m = re.search(r"\b(?:colou?r code|colou?r|make|set)\b\s+(?:the )?([\w ]+?)\s+(?:colou?r(?:\s+to)?\s+)?" + cw + r"\b", text, re.I)
    if not m:
        return None
    name = _clean_name(_NOISE.sub("", m.group(1)))
    if not name:
        return None
    return [{"type": "set_track_color", "name": name, "colour": vocab.COLORS[cw]}]


# priority order: most specific first
_MATCHERS = [
    _m_rename,
    _m_group,
    _m_color,
    _m_create_with_plugins,
    _m_add_plugin,
    _m_create,
    _m_delete,
    _m_mute,
    _m_solo,
]


def parse(text: str) -> str:
    text = text.strip()
    for matcher in _MATCHERS:
        try:
            actions = matcher(text)
        except Exception:
            actions = None
        if actions:
            return dsl.render(actions)
    return ""


if __name__ == "__main__":
    for line in sys.stdin:
        line = line.strip()
        if line:
            print(repr(parse(line)))
