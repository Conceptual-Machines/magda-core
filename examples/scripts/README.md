# MAGDA Lua Controller Scripts

Drop these into:

| OS | Path |
|----|------|
| macOS | `~/Library/Application Support/MAGDA/Scripts/Controllers/` |
| Windows | `%APPDATA%\MAGDA\Scripts\Controllers\` |
| Linux | `~/.config/MAGDA/Scripts/Controllers/` |

MAGDA loads the first script (alphabetically) at startup. Use **Settings → Controllers → Reload Lua Script** after editing.

## API

A script can define any of:

- `on_load()` — fires after the script is evaluated. Use for handshakes / LED priming.
- `on_unload()` — fires before teardown. Use for restoring the device to a clean state on shutdown.
- `on_midi(e)` — fires per inbound MIDI event.
- `on_tick(dt)` — fires at ~30 Hz on the message thread; `dt` is seconds since the last tick. Use for LED animations.

Inbound event table:

| field | type | values |
|---|---|---|
| `type` | string | `cc`, `note_on`, `note_off`, `pitch_bend`, `aftertouch`, `program_change`, `sysex`, `other` |
| `channel` | int | 1..16 (0 for sysex) |
| `number` | int | CC#, note#, program#; 0 for pitch_bend and channel-pressure aftertouch; note# for poly-aftertouch |
| `value` | int | CC value 0..127, velocity 0..127, pitch_bend −8192..8191, aftertouch pressure 0..127 |
| `bytes` | array | sysex only: 1-indexed array of integer bytes (no F0/F7 framing) |
| `port` | string | originating device's display name |

Available bindings (all on the message thread):

- `magda.log.{info, warn, error}(...)`
- `magda.selection.{track, clip, clips, has_notes, note_clip, note_indices, select_track, select_tracks, select_clip, select_clips, clear_notes}`
- `magda.tracks.{create, delete, count, list, get, set_name, set_volume, set_pan, set_muted, set_soloed}`
- `magda.clips.{create_midi, delete, list_on_track, list_arrangement, set_name, set_groove}`
- `magda.session.{launch_clip, stop_clip, stop_track, stop_all, active_clip_on_track}`
- `magda.project.info()` — name, file_path, tempo, time_sig_num, time_sig_den, sample_rate, loop_enabled
- `magda.midi.{send, send_cc, send_note_on, send_note_off, send_sysex, outputs}` — host → device output. SysEx payload is the bytes between F0 and F7; the binding adds the framing.
- `magda.transport.{play, stop, set_recording, is_playing, is_recording, is_loop_enabled, set_loop_enabled, position_beats, set_position_beats}` — beats-authoritative position.
- `magda.focused.{has_focus, name, macro_name, macro_value, set_macro}` — read / write the focused device's 16 macros.

## Sandbox

Disabled in scripts: `os.execute`, `os.remove`, `os.rename`, `os.exit`, `os.getenv`, `dofile`, `loadfile`, `load`, `require`, the `io`, `package`, and `debug` tables.

Available: `string`, `math`, `table`, `utf8`, `coroutine`, `os.{date,time,difftime,clock}`, and `print` (routes to MAGDA's log).
