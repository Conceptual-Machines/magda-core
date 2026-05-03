# Lua Scripting Reference

MAGDA hosts user **Lua 5.4** scripts to drive MIDI controllers and automate the DAW. Every script runs in a sandboxed runtime — no `io`, `os`, `package`, or `require`; just Lua's standard `string`, `math`, `table`, and `coroutine` libraries plus the `magda.*` API.

!!! warning "API stability"
    The `magda.*` surface is **unstable in 0.7**. Method names, argument shapes, and which fields are exposed on snapshot tables are likely to change in 0.8 as more controllers are scripted against it. Pin a script to a release if you want it to keep working.

For an end-to-end overview of how to load scripts and the footer indicator, see [Controllers — Lua Controller Scripts](../interface/controllers.md#lua-controller-scripts).

## Script Lifecycle

A script is a plain `.lua` file. Top-level code runs once when the script is loaded; after that, MAGDA dispatches events to three optional callback functions:

| Callback | When it fires | Arguments |
|---|---|---|
| `on_load()` | Once after the script is loaded and the `magda` table is installed | none |
| `on_midi(event)` | For every incoming MIDI event matching the script's assigned input port (or every event on every input, if the port is left blank) | the event table — see below |
| `on_tick(dt)` | ~30 Hz | `dt` — seconds since the last tick (number) |

All three are called on the message thread. MIDI input arrives on the audio thread but is hopped to the message thread before `on_midi` fires, so the entire `magda.*` API is safe to call from anywhere.

```lua
function on_load()
  magda.log.info("script loaded, MAGDA " .. magda.app.version())
end

function on_midi(event)
  -- ...
end

function on_tick(dt)
  -- e.g. blink an LED every half second
end
```

### MIDI Event Shape

The `event` table passed to `on_midi` has these fields:

| Field | Type | Description |
|---|---|---|
| `type` | string | `"cc"`, `"note_on"`, `"note_off"`, `"pitch_bend"`, `"aftertouch"`, `"program_change"`, `"sysex"`, or `"other"` |
| `channel` | integer | MIDI channel 1..16 (0 for sysex — no channel) |
| `number` | integer | CC number, note number, program number, or 0 (pitch bend, channel-pressure aftertouch, sysex) |
| `value` | integer | CC value 0..127, velocity 0..127, pitch-bend −8192..8191, aftertouch pressure 0..127 |
| `bytes` | array of integers | Sysex only — the payload bytes, 1-indexed, **not** including F0/F7 framing |
| `port` | string | Display name of the originating MIDI input |

## API Reference

Every function below lives under the global `magda` table. All are message-thread safe.

### `magda.log`

| Function | Description |
|---|---|
| `magda.log.info(msg)` | Log at info level (visible in MAGDA's debug console) |
| `magda.log.warn(msg)` | Log at warn level |
| `magda.log.error(msg)` | Log at error level |

### `magda.app`

| Function | Returns | Description |
|---|---|---|
| `magda.app.version()` | string | MAGDA version (e.g. `"0.7.0"`) |

### `magda.transport`

| Function | Returns | Description |
|---|---|---|
| `magda.transport.play()` | — | Start playback |
| `magda.transport.stop()` | — | Stop playback |
| `magda.transport.set_recording(enabled)` | — | Set the record-arm state of the transport |
| `magda.transport.is_playing()` | boolean | |
| `magda.transport.is_recording()` | boolean | |
| `magda.transport.is_loop_enabled()` | boolean | |
| `magda.transport.set_loop_enabled(enabled)` | — | |
| `magda.transport.position_beats()` | number | Current playhead position, in beats |
| `magda.transport.set_position_beats(beats)` | — | Move the playhead |

### `magda.project`

| Function | Returns | Description |
|---|---|---|
| `magda.project.info()` | table | `{ name, file_path, tempo, time_sig_num, time_sig_den, sample_rate, loop_enabled }` |

### `magda.selection`

| Function | Returns | Description |
|---|---|---|
| `magda.selection.track()` | integer or nil | Selected track id |
| `magda.selection.clip()` | integer or nil | Selected clip id |
| `magda.selection.clips()` | array of integers | All selected clip ids (order unspecified) |
| `magda.selection.has_notes()` | boolean | A note selection is active inside a MIDI editor |
| `magda.selection.note_clip()` | integer or nil | Clip id the note selection lives in |
| `magda.selection.note_indices()` | array of integers | Selected note indices within that clip |
| `magda.selection.select_track(id)` | — | |
| `magda.selection.select_tracks({id, id, ...})` | — | Multi-select |
| `magda.selection.select_clip(id)` | — | |
| `magda.selection.select_clips({id, id, ...})` | — | Multi-select |
| `magda.selection.clear_notes()` | — | Clear note selection |

### `magda.tracks`

| Function | Returns | Description |
|---|---|---|
| `magda.tracks.create(name [, type])` | integer | Track id; `type` is `"audio"` (default), `"group"`, `"aux"`, `"master"`, or `"multi_out"` |
| `magda.tracks.delete(id)` | — | |
| `magda.tracks.count()` | integer | |
| `magda.tracks.list()` | array of tables | Each table: `{ id, name, type, volume, pan, muted, soloed, record_armed, frozen }` |
| `magda.tracks.get(id)` | table or nil | Same shape as the list entries |
| `magda.tracks.set_name(id, name)` | — | |
| `magda.tracks.set_volume(id, value)` | — | Linear gain 0..1 |
| `magda.tracks.set_pan(id, value)` | — | -1..1 |
| `magda.tracks.set_muted(id, bool)` | — | |
| `magda.tracks.set_soloed(id, bool)` | — | |

### `magda.clips`

| Function | Returns | Description |
|---|---|---|
| `magda.clips.create_midi(track_id, start_beats, length_beats)` | integer | New MIDI clip id |
| `magda.clips.delete(id)` | — | |
| `magda.clips.list_on_track(track_id)` | array of integers | Clip ids on that track |
| `magda.clips.list_arrangement()` | array of tables | Each table: `{ id, track_id, name, start_beats, length_beats }` |
| `magda.clips.set_name(id, name)` | — | |
| `magda.clips.set_groove(id, template_name)` | — | Apply a groove template by name |
| `magda.clips.colour(id)` | table or nil | `{ r, g, b }` with each component 0..127 (7-bit, sysex-ready) |

### `magda.session`

| Function | Returns | Description |
|---|---|---|
| `magda.session.launch_clip(clip_id)` | — | Launch a session clip |
| `magda.session.stop_clip(clip_id)` | — | |
| `magda.session.stop_track(track_id)` | — | Stop the currently-active session clip on a track |
| `magda.session.stop_all()` | — | |
| `magda.session.launch_scene(scene_index)` | — | 0-based; tracks with empty slots in that row have their active clip stopped (matches the UI scene-button click) |
| `magda.session.active_clip_on_track(track_id)` | integer or nil | Currently-playing session clip on the track |
| `magda.session.clip_in_slot(track_id, scene_index)` | integer or nil | Clip in a specific slot, or nil if empty |
| `magda.session.clip_play_state(clip_id)` | string | `"stopped"`, `"queued"`, or `"playing"` |
| `magda.session.set_view(scene_offset [, scene_count])` | — | Publish the controller's visible scene window so MAGDA's UI can highlight it |

### `magda.focused`

The "focused" device is the device currently shown in the parameter view. Profiles with `focused.macro.*` resolvers and the AI panel both read it.

| Function | Returns | Description |
|---|---|---|
| `magda.focused.has_focus()` | boolean | A device is focused |
| `magda.focused.name()` | string | Display name of the focused device |
| `magda.focused.macro_name(index)` | string | Name of macro `index` (0..7) |
| `magda.focused.macro_value(index)` | number | Current value 0..1 |
| `magda.focused.set_macro(index, value)` | — | Write a macro value 0..1 |
| `magda.focused.cycle_device(direction)` | — | Move focus through the chain — `+1` next, `-1` previous |
| `magda.focused.auto_map()` | — | Engage automap so the controller's profile drives the focused device's macros |
| `magda.focused.clear_auto_map()` | — | Disengage automap |

### `magda.midi`

For driving feedback (motorised faders, LED rings, screens) back to the controller. The `port` argument is the display name of a MIDI **output** — pass `"@default"` (or the empty string) to use the script's assigned output port.

| Function | Returns | Description |
|---|---|---|
| `magda.midi.send(port, status, data1 [, data2])` | boolean | Raw channel-voice message; returns true if the port was found |
| `magda.midi.send_cc(port, channel, number, value)` | boolean | |
| `magda.midi.send_note_on(port, channel, note, velocity)` | boolean | |
| `magda.midi.send_note_off(port, channel, note)` | boolean | |
| `magda.midi.send_sysex(port, {byte, byte, ...})` | boolean | F0/F7 framing is added by the binding — pass payload bytes only |
| `magda.midi.outputs()` | array of strings | Names of every connected MIDI output port |
| `magda.midi.default_output()` | string | The script's currently-assigned output port |

## Examples

### Sustain pedal launches the selected session clip

```lua
function on_midi(e)
  if e.type ~= "cc" or e.number ~= 64 then return end

  local track = magda.selection.track()
  if not track then return end

  if e.value >= 64 then
    local clip = magda.selection.clip()
    if clip then magda.session.launch_clip(clip) end
  else
    magda.session.stop_track(track)
  end
end
```

### Eight knobs map to track volumes

```lua
function on_midi(e)
  if e.type ~= "cc" then return end
  if e.number < 1 or e.number > 8 then return end

  magda.tracks.set_volume(e.number, e.value / 127.0)
end
```

### Light the play button while the transport is rolling

```lua
local PLAY_NOTE = 60
local was_playing = false

function on_tick(_dt)
  local playing = magda.transport.is_playing()
  if playing == was_playing then return end
  was_playing = playing

  if playing then
    magda.midi.send_note_on("@default", 1, PLAY_NOTE, 127)
  else
    magda.midi.send_note_off("@default", 1, PLAY_NOTE)
  end
end
```

## Bundled Examples

Working scripts ship in the `examples/scripts/` folder of the repo:

- `foot-pedal.lua` — sustain-pedal launches the selected session clip
- `8-knobs.lua` — eight CCs drive eight track volumes
- `launchpad.lua` — Novation Launchpad clip launcher with LED feedback
- `launchkey_mini_mk4.lua` — full Launchkey Mini MK4 surface (transport, pads, knobs, sceneswitching)

Copy any of them into your scripts folder (see [Controllers — Scripts Folder](../interface/controllers.md#scripts-folder)) and load via the Controllers dialog.
