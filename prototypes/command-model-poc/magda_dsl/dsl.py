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

    if t == "unsupported":
        # Out of scope: render nothing so the caller can say so, rather than
        # executing the nearest intent. A closed label set has no way to
        # abstain unless abstaining is itself a label — without this,
        # "mute the guitar" (deliberately not a command any more) landed on
        # groove.list() and ran it.
        return []

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

    if t == "create_rack":
        # A rack goes on a track; devices chain onto rack.new so they land
        # inside the rack chain (interpreter routes fx.add into the active
        # chain). All devices stay on ONE line for that reason.
        line = f'{_track_ref(a)}.rack.new()'
        for p in a.get("plugins", []):
            line += f'.fx.add(name={q(p)})'
        return [line]

    if t == "create_rack_parallel":
        # Same rack, but each device gets its OWN chain so they run in
        # parallel rather than in series. fx.add lands in the active chain, and
        # rack.chain_new opens a new one — so the first device goes into the
        # rack's default chain and every later device follows a chain_new.
        line = f'{_track_ref(a)}.rack.new()'
        for i, p in enumerate(a.get("plugins", [])):
            if i:
                line += '.rack.chain_new()'
            line += f'.fx.add(name={q(p)})'
        return [line]

    if t == "add_plugin":
        return [f'{_track_ref(a)}.fx.add(name={q(a["plugin"])})']

    if t == "rename_track":
        return [f'{_track_ref(a)}.track.set(name={q(a["new_name"])})']

    # mute_track / solo_track are retired from the ENCODER's intent set (real-time
    # mixer states, not edits) but stay renderable: the conv net's shipped
    # weights still predict them and both models share this renderer.
    if t == "mute_track":
        return [f'{_track_ref(a)}.track.set(mute=true)']

    if t == "solo_track":
        return [f'{_track_ref(a)}.track.set(solo=true)']

    if t == "delete_track":
        if a.get("all"):
            return [f'filter(tracks, track.name == {q(a["name"])}).delete()']
        return [f'{_track_ref(a)}.delete()']

    if t == "set_track_volume":
        return [f'{_track_ref(a)}.track.set(volume_db={a["volume_db"]:g})']

    if t == "set_track_pan":
        return [f'{_track_ref(a)}.track.set(pan={a["pan"]:g})']

    if t == "set_track_color":
        return [f'{_track_ref(a)}.track.set(colour={q(a["colour"])})']

    if t == "group_tracks":
        anchor = a["ids"][0]
        ids = ",".join(str(i) for i in a["ids"])
        return [f'track(id={anchor}).track.group(name={q(a["name"])}, tracks={q(ids)})']

    if t == "select_tracks":
        # 1-based index into the state snapshot, which is what track(id=N)
        # already means — no new grammar needed.
        return [f'track(id={a["id"]}).select()']

    if t == "select_all_clips":
        return [f'{_track_ref(a)}.clips.select()']

    if t == "select_all_clips_rename":
        return [f'{_track_ref(a)}.clips.select().clip.rename(name={q(a["new_name"])})']

    if t == "select_clips_named":
        return [f'{_track_ref(a)}.clips.select(clip.name == {q(a["clip_name"])})']

    if t == "select_clips_type":
        return [f'{_track_ref(a)}.clips.select(clip.type == {q(a["clip_type"])})']

    if t == "select_clips_longer_than":
        return [f'{_track_ref(a)}.clips.select(clip.length_bars > {a["bars"]:g})']

    if t == "select_clips_shorter_than":
        return [f'{_track_ref(a)}.clips.select(clip.length_bars < {a["bars"]:g})']

    if t == "select_clips_length_at_least":
        return [f'{_track_ref(a)}.clips.select(clip.length_bars >= {a["bars"]:g})']

    if t == "select_clips_length_at_most":
        return [f'{_track_ref(a)}.clips.select(clip.length_bars <= {a["bars"]:g})']

    if t == "select_clips_length_exactly":
        return [f'{_track_ref(a)}.clips.select(clip.length_bars == {a["bars"]:g})']

    if t == "select_clips_not_named":
        return [f'{_track_ref(a)}.clips.select(clip.name != {q(a["clip_name"])})']

    if t == "select_clips_starting_after":
        return [f'{_track_ref(a)}.clips.select(clip.start_bar >= {a["bar"]:g})']

    if t == "select_clips_starting_before":
        return [f'{_track_ref(a)}.clips.select(clip.start_bar <= {a["bar"]:g})']

    # --- clip ops ---------------------------------------------------------
    if t == "clip_new":
        # `bar` positions the clip; omitted, the interpreter auto-places after
        # the last clip on the track. Length defaults to 4 bars there too, but
        # we always emit it so the DSL reads unambiguously.
        if a.get("bar") is not None:
            return [f'{_track_ref(a)}.clip.new(bar={a["bar"]:g}, '
                    f'length_bars={a["length_bars"]:g})']
        return [f'{_track_ref(a)}.clip.new(length_bars={a["length_bars"]:g})']

    if t == "clip_mute":
        # Disable clips: the ones a preceding clips.select matched, or a
        # specific index. A disabled clip stays on the timeline but is silent.
        enabled = "true" if a.get("enabled") else "false"
        if a.get("clip_name"):
            return [f'{_track_ref(a)}.clips.select(clip.name == {q(a["clip_name"])})'
                    f'.clip.set(enabled={enabled})']
        if a.get("index") is not None:
            return [f'{_track_ref(a)}.clip.set(index={a["index"]:g}, enabled={enabled})']
        return [f'{_track_ref(a)}.clip.set(enabled={enabled})']

    if t == "clip_rename":
        return [f'{_track_ref(a)}.clip.rename(name={q(a["clip_name"])})']

    if t == "clip_delete":
        return [f'{_track_ref(a)}.clip.delete(index={a["index"]:g})']

    # --- track move -------------------------------------------------------
    if t == "track_move":
        return [f'{_track_ref(a)}.track.move(index={a["index"]:g})']

    # --- MIDI note ops (operate on the track's selected notes) ------------
    if t == "notes_delete":
        return [f'{_track_ref(a)}.notes.delete()']

    if t == "notes_transpose":
        return [f'{_track_ref(a)}.notes.transpose(semitones={a["semitones"]:g})']

    if t == "notes_set_velocity":
        return [f'{_track_ref(a)}.notes.set_velocity(value={a["value"]:g})']

    if t == "notes_resize":
        return [f'{_track_ref(a)}.notes.resize(length={a["length"]:g})']

    if t == "notes_quantize":
        return [f'{_track_ref(a)}.notes.quantize(grid={a["grid"]:g})']

    if t == "notes_set_pitch":
        return [f'{_track_ref(a)}.notes.set_pitch(pitch={a["pitch"]})']

    if t == "notes_select_pitch":
        return [f'{_track_ref(a)}.notes.select(note.pitch == {a["pitch"]})']

    if t == "notes_select_velocity_above":
        return [f'{_track_ref(a)}.notes.select(note.velocity > {a["value"]:g})']

    if t == "notes_select_velocity_below":
        return [f'{_track_ref(a)}.notes.select(note.velocity < {a["value"]:g})']

    # --- groove (timing/swing) -------------------------------------------
    if t == "groove_set":
        return [f'groove.set(template={q(a["template"])}, strength={a["strength"]:g})']

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
