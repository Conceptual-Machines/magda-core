-- launchkey_mini_mk4.lua
-- Novation Launchkey Mini MK4 controller script for MAGDA.
--
-- What it does:
--   * Enters DAW mode on load (so pads / transport row report on the DAW port).
--   * Pads (2x8) launch session clips on the first 8 tracks, two clip slots per track.
--   * Transport row drives MAGDA's transport (play / stop / record / loop).
--   * Knobs are NOT handled here — they're already wired to focused-device
--     macros via resources/controllers/novation.launchkey_mini_mk4.macros.json.
--   * Exits DAW mode on unload so the device returns to a clean Standalone
--     state when MAGDA quits.
--
-- Reference: docs/controllers/launchkey_mk4_programmers_reference_v2.pdf
--
-- Drop into:
--   macOS:   ~/Library/MAGDA/Scripts/Controllers/
--   Windows: %APPDATA%\MAGDA\Scripts\Controllers\
--   Linux:   ~/.config/MAGDA/Scripts/Controllers/

----------------------------------------------------------------
-- Device port matching
----------------------------------------------------------------
-- The MK4 exposes two USB MIDI interfaces: a "MIDI" port (keys / wheels /
-- pad Custom Modes) and a "DAW" port (control surface). Select the DAW
-- protocol ports as Port Out / Port In in MAGDA's Lua Scripts row.

local SCRIPT_LABEL = "LK Mini"
local DISPLAY_VALUE_MAX_CHARS = 16

-- Scene offset = sceneIndex of the top pad row. Bottom row is offset+1.
local scene_offset = 0

local function publish_session_view(sceneOffset, sceneCount)
  if not (magda and magda.session and magda.session.set_view) then return end

  local ok, err = pcall(magda.session.set_view, sceneOffset, sceneCount)
  if not ok then
    magda.log.warn("[launchkey] session view highlight failed: "..tostring(err))
  end
end

local function is_daw_port(port)
  return port:lower():find("launchkey") and port:lower():find("daw")
end

-- Output port name for sends (DAW Out). Resolved on first send.
local daw_out = nil
local function find_daw_out()
  if daw_out then return daw_out end
  local configured = magda.midi.default_output()
  if configured and configured ~= "" then
    daw_out = configured
    return configured
  end
  for _, name in ipairs(magda.midi.outputs()) do
    if is_daw_port(name) then
      daw_out = name
      return name
    end
  end
  return nil
end

----------------------------------------------------------------
-- DAW-mode handshake
----------------------------------------------------------------
-- "Enable DAW Mode" is a note-on, NOT SysEx, despite living in the SysEx
-- chapter of the reference: 9Fh 0Ch 7Fh = note-on channel 16 #12 vel 127.
-- Disable: same with vel 0.

local function set_daw_mode(enable)
  local out = find_daw_out()
  if not out then
    magda.log.warn("[launchkey] no DAW Out port matched - is the device connected?")
    return
  end
  magda.log.info("[launchkey] "..(enable and "entering" or "leaving").." DAW mode via port: "..out)
  magda.midi.send_note_on(out, 16, 0x0C, enable and 0x7F or 0x00)
end

----------------------------------------------------------------
-- Stationary display banner ("MAGDA")
----------------------------------------------------------------
-- Reference p.17-19. The stationary display (target 0x20) is what's
-- shown when no temp display is up. We configure it once with
-- arrangement 1 (2-line: Parameter Name + Text Parameter Value) and
-- write MAGDA to the Name field and version/script info to the Value
-- field. Bits 6+5 of the config byte stay set (default) so encoder Temp Displays still
-- trigger normally on knob change/touch.
local STATIONARY_TARGET = 0x20

local function send_display_text(out, target, field, text)
  local payload = {0x00, 0x20, 0x29, 0x02, 0x13, 0x06, target, field}
  for i = 1, #text do
    local b = text:byte(i)
    if b < 0x20 or b > 0x7E then b = 0x3F end
    table.insert(payload, b)
  end
  magda.midi.send_sysex(out, payload)
end

local function get_app_version()
  if magda and magda.app and magda.app.version then
    local ok, version = pcall(magda.app.version)
    if ok and version and version ~= "" then return version end
  end

  local ok, info = pcall(magda.project.info)
  if ok and info and info.version and info.version ~= "" then return info.version end

  return "dev"
end

local function display_subtitle()
  local version = get_app_version()
  local full = "v"..version.." "..SCRIPT_LABEL
  if #full <= DISPLAY_VALUE_MAX_CHARS then return full end

  local baseVersion = version:match("^[^-+]+") or version
  local compact = "v"..baseVersion.." "..SCRIPT_LABEL
  if #compact <= DISPLAY_VALUE_MAX_CHARS then return compact end

  local versionOnly = "v"..baseVersion
  if #versionOnly <= DISPLAY_VALUE_MAX_CHARS then return versionOnly end

  return versionOnly:sub(1, DISPLAY_VALUE_MAX_CHARS)
end

local function show_stationary_banner(out, title, subtitle)
  if not out then
    magda.log.warn("[launchkey] banner: no DAW Out port")
    return
  end
  -- Configure: bit 6 (auto temp on Change) + bit 5 (auto temp on Touch)
  -- + arrangement 1 = 0x60 | 0x01 = 0x61.
  magda.midi.send_sysex(out, {0x00, 0x20, 0x29, 0x02, 0x13, 0x04, STATIONARY_TARGET, 0x61})
  send_display_text(out, STATIONARY_TARGET, 0x00, title)
  send_display_text(out, STATIONARY_TARGET, 0x01, subtitle)
  -- Trigger the display so the new contents come up immediately
  -- (config byte 0x7F per reference p.18, "compact way to trigger
  -- display").
  magda.midi.send_sysex(out, {0x00, 0x20, 0x29, 0x02, 0x13, 0x04, STATIONARY_TARGET, 0x7F})
end

----------------------------------------------------------------
-- Pad layout
----------------------------------------------------------------
-- DAW-mode pad notes (Channel 1):
--   Top row:    96..103 (0x60..0x67)
--   Bottom row: 112..119 (0x70..0x77)
-- Columns map to the first 8 tracks in MAGDA's current track order. Track IDs
-- are persistent IDs, not display columns, so resolve the current column
-- through magda.tracks.list() every time instead of assuming IDs are 1..8.
-- Rows map to scene indices in visual order: top pad row = scene 0 (visually
-- highest in MAGDA's session view), bottom pad row = scene 1.

local function track_id_for_column(col)
  local tracks = magda.tracks.list()
  local track = tracks[col + 1]
  return track and track.id or nil
end

local function pad_to_track_and_scene(note)
  if note >= 0x60 and note <= 0x67 then
    return track_id_for_column(note - 0x60), scene_offset       -- top row -> scene_offset
  elseif note >= 0x70 and note <= 0x77 then
    return track_id_for_column(note - 0x70), scene_offset + 1   -- bottom row -> scene_offset + 1
  end
  return nil, nil
end

----------------------------------------------------------------
-- Transport row (DAW-port CCs on Channel 16)
----------------------------------------------------------------
-- Mini's transport area in DAW mode (reference fig. 3 p. 9):
--   Play   = CC 115 (0x73)
--   Record = CC 117 (0x75)
-- The Mini has no dedicated Stop button. Stop is Shift+Play. Play here
-- toggles play/stop, which covers the common case without needing to
-- track Shift state.
local TRANSPORT_PLAY   = 0x73
local TRANSPORT_RECORD = 0x75

-- Side buttons (left of the pads, labelled "< Track" / "Track >" by
-- Novation but we use them for scene-bank navigation: scroll the pad
-- window up / down through session scenes. With only 16 pads = 8 tracks
-- x 2 scenes visible, this is the way to reach scenes 3, 4, 5, ...
local SCENE_PREV = 0x6A   -- 106 - top button (< Track), scrolls up
local SCENE_NEXT = 0x6B   -- 107 - bottom button (Track >), scrolls down

-- Right-side buttons (next to the pad rows). Used for track selection:
-- cycle the focused/selected track one step at a time.
local TRACK_PREV = 0x68   -- 104 - top right, previous track
local TRACK_NEXT = 0x69   -- 105 - bottom right, next track

-- Device-cycle buttons: select prev/next device on the selected track.
local DEVICE_PREV = 0x33  -- 51
local DEVICE_NEXT = 0x34  -- 52

----------------------------------------------------------------
-- Lifecycle
----------------------------------------------------------------

function on_load()
  magda.log.info("[launchkey] loading")
  -- Surface every output port name so we can see what JUCE is exposing.
  -- If "DAW" doesn't show up here, the matcher won't find it and the
  -- handshake never reaches the device.
  for i, name in ipairs(magda.midi.outputs()) do
    magda.log.info("[launchkey] output["..i.."] = "..name)
  end
  set_daw_mode(true)
  -- Tell MAGDA the controller's 8 knobs auto-map to the focused device's
  -- macros 0..7. Installs sentinel `focused.macro:0..7` resolver bindings
  -- so the green automap dot lights on the focused device's header and
  -- on its macros. The actual knob -> macro routing still happens via
  -- magda.focused.set_macro in on_midi (DAW-port CCs don't reach
  -- ControllerRouter, so the registered bindings aren't routable — they
  -- exist purely for the UI affordance).
  magda.focused.auto_map()
  publish_session_view(scene_offset, 2)
  -- Paint "MAGDA" on the device's stationary display so the LCD reads
  -- our brand at rest. Temp displays (encoder names, etc.) overlay on
  -- top and revert to MAGDA after their timeout.
  show_stationary_banner(find_daw_out(), "MAGDA", display_subtitle())
end

function on_unload()
  magda.log.info("[launchkey] unloading")
  -- Drop the automap sentinel bindings so the green dot disappears
  -- when the script is gone.
  magda.focused.clear_auto_map()
  -- Clear all pad LEDs before leaving DAW mode so the device returns
  -- to a blank state.
  publish_session_view(0, 0)
  local out = find_daw_out()
  if out then
    for col = 0, 7 do
      magda.midi.send_note_on(out, 1, 0x60 + col, 0)
      magda.midi.send_note_on(out, 1, 0x70 + col, 0)
    end
  end
  set_daw_mode(false)
end

----------------------------------------------------------------
-- LED feedback (pads reflect session-clip playback state)
----------------------------------------------------------------
-- LED protocol on the MK4:
--   Channel 1 note-on: stationary palette colour
--   Channel 2 note-on: flashing palette colour
--   Channel 3 note-on: pulsing palette colour
-- Velocity = palette index 0..127 (reference p. 15).
--
-- For the stopped-clip state we use the SysEx RGB form (Mini header
-- F0 00 20 29 02 13 01 43 <padId> <r> <g> <b> F7) so the pad mirrors the
-- clip's MAGDA colour exactly. Flashing/pulsing only exist on the
-- channel-based palette path, so queued/playing fall back to fixed
-- palette colours.

-- Mini-SKU SysEx header for the per-pad RGB command.
local RGB_PREFIX = {0x00, 0x20, 0x29, 0x02, 0x13, 0x01, 0x43}

local PAD_NOTES = {
  [0] = {0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67},  -- top row
  [1] = {0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77},  -- bottom row
}

-- Cache the last-sent LED command per pad so on_tick only emits MIDI
-- when the colour actually changes. Cache value is a string signature
-- covering both palette ('p:<ch>:<vel>') and RGB ('rgb:<r>:<g>:<b>') paths.
local last_led = {}

local function send_palette(out, note, channel, velocity)
  local sig = string.format("p:%d:%d", channel, velocity)
  if last_led[note] == sig then return end
  last_led[note] = sig
  magda.midi.send_note_on(out, channel, note, velocity)
end

local function send_rgb(out, note, r, g, b)
  local sig = string.format("rgb:%d:%d:%d", r, g, b)
  if last_led[note] == sig then return end
  last_led[note] = sig
  local payload = {RGB_PREFIX[1], RGB_PREFIX[2], RGB_PREFIX[3], RGB_PREFIX[4],
                   RGB_PREFIX[5], RGB_PREFIX[6], RGB_PREFIX[7],
                   note, r, g, b}
  magda.midi.send_sysex(out, payload)
end

local function refresh_leds()
  local out = find_daw_out()
  if not out then return end

  for row = 0, 1 do
    for col = 0, 7 do
      local note = PAD_NOTES[row][col + 1]
      local track = track_id_for_column(col)
      local scene = scene_offset + row
      local clip = track and magda.session.clip_in_slot(track, scene) or nil

      if not clip then
        send_palette(out, note, 1, 0)        -- off
      else
        local state = magda.session.clip_play_state(clip)
        if state == 'playing' then
          send_palette(out, note, 3, 21)     -- green pulsing
        elseif state == 'queued' then
          send_palette(out, note, 2, 13)     -- yellow flashing
        else
          -- Stopped: paint the pad with the clip's MAGDA colour via SysEx.
          local rgb = magda.clips.colour(clip)
          if rgb then
            send_rgb(out, note, rgb.r, rgb.g, rgb.b)
          else
            send_palette(out, note, 1, 45)   -- fallback blue
          end
        end
      end
    end
  end
end

----------------------------------------------------------------
-- Encoder name display (small LCD shows macro names on knob touch)
----------------------------------------------------------------
-- Mini-SKU "Set text" SysEx (reference p. 19):
--   F0 00 20 29 02 13 06 <target> <field> <text…> F7
-- Targets 0x15..0x1C are the per-encoder Temp Displays (1..8). Field 0
-- is the Name slot of the default arrangement (Parameter Name + Numeric
-- Value), so writing the macro name there makes the device flash it
-- whenever the matching knob is touched / turned.
local SET_TEXT_PREFIX = {0x00, 0x20, 0x29, 0x02, 0x13, 0x06}
local ENCODER_NAME_TARGET_BASE = 0x15
local ENCODER_NAME_MAX_CHARS = 16

local last_encoder_name = {}

local function send_encoder_name(out, encoder_idx, name)
  -- Truncate + sanitise: device only accepts ASCII 0x20..0x7E (plus a
  -- handful of reassigned control codes we don't use). Anything else
  -- becomes '?' so we never trip the SysEx parser.
  if #name > ENCODER_NAME_MAX_CHARS then
    name = name:sub(1, ENCODER_NAME_MAX_CHARS)
  end
  local payload = {SET_TEXT_PREFIX[1], SET_TEXT_PREFIX[2], SET_TEXT_PREFIX[3],
                   SET_TEXT_PREFIX[4], SET_TEXT_PREFIX[5], SET_TEXT_PREFIX[6],
                   ENCODER_NAME_TARGET_BASE + encoder_idx, 0x00}
  for i = 1, #name do
    local b = name:byte(i)
    if b < 0x20 or b > 0x7E then b = 0x3F end  -- '?'
    table.insert(payload, b)
  end
  magda.midi.send_sysex(out, payload)
end

local last_focus_for_names = nil

local function refresh_encoder_names()
  local out = find_daw_out()
  if not out then return end
  local has_focus = magda.focused.has_focus()
  local focused_name = has_focus and magda.focused.name() or ""
  if focused_name ~= last_focus_for_names then
    -- Focus changed: invalidate the diff cache so every encoder re-sends
    -- (clears when leaving focus, repopulates when entering).
    last_encoder_name = {}
    last_focus_for_names = focused_name
  end
  for i = 0, 7 do
    -- When nothing is focused there are no macros to label — send empty
    -- so the device falls back to its own default for that encoder
    -- instead of carrying stale names from a previous focus.
    local name = has_focus and (magda.focused.macro_name(i) or "") or ""
    if last_encoder_name[i] ~= name then
      last_encoder_name[i] = name
      send_encoder_name(out, i, name)
    end
  end
end

----------------------------------------------------------------
-- Transport button LEDs
----------------------------------------------------------------
-- Play and Record are monochrome LEDs. Per the reference (p. 14)
-- monochrome LEDs are addressed with a CC on Channel 4: CC number =
-- the LED's CC index, value = brightness (0..127). So the same CC
-- number the button generates, sent back on Ch4, drives its lamp.
local last_play_lit = nil
local last_rec_lit = nil

local function set_mono_led(out, cc, lit)
  magda.midi.send_cc(out, 4, cc, lit and 0x7F or 0x00)
end

local function refresh_transport_leds()
  local out = find_daw_out()
  if not out then return end
  local playing = magda.transport.is_playing()
  if playing ~= last_play_lit then
    last_play_lit = playing
    set_mono_led(out, TRANSPORT_PLAY, playing)
  end
  local recording = magda.transport.is_recording()
  if recording ~= last_rec_lit then
    last_rec_lit = recording
    set_mono_led(out, TRANSPORT_RECORD, recording)
  end
end

function on_tick(dt)
  refresh_leds()
  refresh_encoder_names()
  refresh_transport_leds()
end

----------------------------------------------------------------
-- MIDI dispatch
----------------------------------------------------------------

local function handle_pad_press(e)
  local track, scene = pad_to_track_and_scene(e.number)
  if not track then return end

  local clip = magda.session.clip_in_slot(track, scene)
  magda.log.info(string.format(
    "[launchkey] pad note=%d -> track_id=%d scene=%d clip=%s",
    e.number, track, scene, tostring(clip)))
  if clip then
    magda.session.launch_clip(clip)
  end
end

local function cycle_track(direction)
  local tracks = magda.tracks.list()
  if #tracks == 0 then return end

  local current = magda.selection.track()
  local idx = 1
  if current then
    for i, t in ipairs(tracks) do
      if t.id == current then idx = i; break end
    end
  end

  local next_idx = ((idx - 1 + direction) % #tracks) + 1
  local target = tracks[next_idx]
  magda.log.info(string.format(
    "[launchkey] cycle_track  idx %d -> %d  selecting id=%d name='%s'",
    idx, next_idx, target.id, target.name))
  magda.selection.select_track(target.id)
end

local function shift_scene_bank(direction)
  -- Floor at 0; no upper clamp because we don't know how many scenes
  -- the project has. clip_in_slot just returns nil past the last scene,
  -- so the worst case is empty pads (which the LED renderer handles).
  scene_offset = math.max(0, scene_offset + direction)
  publish_session_view(scene_offset, 2)
  magda.log.info(string.format(
    "[launchkey] scene_offset = %d (top row) / %d (bottom row)",
    scene_offset, scene_offset + 1))
end

local function handle_transport_cc(e)
  if e.value == 0 then return end   -- ignore button release

  if e.number == TRANSPORT_PLAY then
    if magda.transport.is_playing() then
      magda.transport.stop()
    else
      magda.transport.play()
    end
  elseif e.number == TRANSPORT_RECORD then
    magda.transport.set_recording(not magda.transport.is_recording())
  elseif e.number == SCENE_PREV then
    shift_scene_bank(-1)
  elseif e.number == SCENE_NEXT then
    shift_scene_bank(1)
  elseif e.number == TRACK_PREV then
    cycle_track(-1)
  elseif e.number == TRACK_NEXT then
    cycle_track(1)
  elseif e.number == DEVICE_PREV then
    magda.focused.cycle_device(-1)
  elseif e.number == DEVICE_NEXT then
    magda.focused.cycle_device(1)
  end
end

-- Set true to dump every event from the DAW port. Use to discover the
-- actual CC numbers / channel for transport buttons or other controls
-- the manual figures aren't clear about.
local DEBUG_LOG_EVENTS = true

function on_midi(e)
  if DEBUG_LOG_EVENTS and e.type ~= 'aftertouch' then
    magda.log.info(string.format(
      "[launchkey] %s ch=%d num=%d val=%d",
      e.type, e.channel, e.number, e.value))
  end

  if e.type == 'note_on' and e.value > 0 then
    handle_pad_press(e)
  elseif e.type == 'cc' then
    -- Encoders in DAW + Plugin mode: CCs 21..28 (0x15..0x1C) drive
    -- macros 0..7 of the focused device. Bypasses the JSON profile,
    -- which can't see DAW-port CCs once the script enters DAW mode.
    if e.number >= 0x15 and e.number <= 0x1C then
      magda.focused.set_macro(e.number - 0x15, e.value / 127.0)
    else
      handle_transport_cc(e)
    end
  end
end
