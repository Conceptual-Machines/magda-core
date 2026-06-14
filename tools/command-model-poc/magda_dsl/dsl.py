"""Canonical rendering of POC command plans to the shipping MAGDA DSL.

An "action" is a dict: {"type": <command>, ...args}. `render(actions)` emits the
canonical DSL string the model is trained to produce and is exact-matched
against. Keeping rendering in one place guarantees the synthetic dataset's gold
labels and the hand-authored test set use identical canonical forms.

Track references
----------------
  by id   -> track(id=N)
  by name -> track(name="X")          (creates-or-references per DSL semantics)
  all     -> filter(tracks, track.name == "X")   (bulk, exact-name match)
"""
from __future__ import annotations


def q(s: str) -> str:
    return '"' + str(s) + '"'


def _track_ref(a: dict) -> str:
    """Render the statement head that targets a track for a chained method."""
    if a.get("all"):
        return f'filter(tracks, track.name == {q(a["name"])})'
    if "id" in a and a["id"] is not None:
        return f'track(id={a["id"]})'
    return f'track(name={q(a["name"])})'


def render_action(a: dict) -> list[str]:
    """Render a single action to one or more canonical DSL lines."""
    t = a["type"]

    if t == "create_track":
        head = f'track(name={q(a["name"])}, new=true)'
        plugins = a.get("plugins", [])
        if not plugins:
            return [head]
        # First plugin chains onto the creation line (matches the surge_xt
        # worked example); remaining plugins reference the track by name.
        lines = [f'{head}.fx.add(name={q(plugins[0])})']
        lines += [f'track(name={q(a["name"])}).fx.add(name={q(p)})' for p in plugins[1:]]
        return lines

    if t == "add_plugin":
        return [f'{_track_ref(a)}.fx.add(name={q(a["plugin"])})']

    if t == "rename_track":
        return [f'{_track_ref(a)}.track.set(name={q(a["new_name"])})']

    if t == "delete_track":
        if a.get("all"):
            return [f'filter(tracks, track.name == {q(a["name"])}).delete()']
        return [f'{_track_ref(a)}.delete()']

    if t == "mute_track":
        return [f'{_track_ref(a)}.track.set(mute=true)']

    if t == "solo_track":
        return [f'{_track_ref(a)}.track.set(solo=true)']

    if t == "set_track_color":
        return [f'{_track_ref(a)}.track.set(colour={q(a["colour"])})']

    if t == "group_tracks":
        anchor = a["ids"][0]
        ids = ",".join(str(i) for i in a["ids"])
        return [f'track(id={anchor}).track.group(name={q(a["name"])}, tracks={q(ids)})']

    raise ValueError(f"unknown action type: {t!r}")


def render(actions: list[dict]) -> str:
    lines: list[str] = []
    for a in actions:
        lines.extend(render_action(a))
    return "\n".join(lines)


def normalize(dsl: str) -> str:
    """Canonicalise DSL text for exact-match comparison: strip each line,
    drop blank lines, collapse internal runs of spaces that are not inside
    quotes is overkill for the POC, so we only trim line-level whitespace."""
    out = []
    for line in dsl.strip().splitlines():
        s = line.strip()
        if s:
            out.append(s)
    return "\n".join(out)
