-- launchkey_mini_mk4.lua
-- Novation Launchkey Mini MK4 controller script for MAGDA.
--
-- What it does:
--   * Enters DAW mode on load (so pads / transport row report on the DAW port).
--   * Pads (2x8) launch session clips on tracks 1..8, two clip slots per track.
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
-- pad Custom Modes) and a "DAW" port (control surface). We listen for
-- pad / transport on the DAW port and send all feedback to the DAW out.
-- Display name varies slightly per OS — match permissively.

local function is_daw_port(port)
  return port:lower():find("launchkey") and port:lower():find("daw")
end

-- Output port name for sends (DAW Out). Resolved on first send.
local daw_out = nil
local function find_daw_out()
  if daw_out then return daw_out end
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
-- Pad layout
----------------------------------------------------------------
-- DAW-mode pad notes (Channel 1):
--   Top row:    96..103 (0x60..0x67)
--   Bottom row: 112..119 (0x70..0x77)
-- Columns map to tracks 1..8. Rows map to scene indices in visual
-- order: top pad row = scene 0 (visually highest in MAGDA's session
-- view), bottom pad row = scene 1.

local function pad_to_track_and_scene(note)
  if note >= 0x60 and note <= 0x67 then
    return note - 0x60 + 1, 0   -- track 1..8, top row = scene 0 (top of session)
  elseif note >= 0x70 and note <= 0x77 then
    return note - 0x70 + 1, 1   -- track 1..8, bottom row = scene 1
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
end

function on_unload()
  magda.log.info("[launchkey] unloading")
  set_daw_mode(false)
end

----------------------------------------------------------------
-- MIDI dispatch
----------------------------------------------------------------

local function handle_pad_press(e)
  local track, scene = pad_to_track_and_scene(e.number)
  if not track then return end

  local clip = magda.session.clip_in_slot(track, scene)
  if clip then
    magda.session.launch_clip(clip)
  end
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
  end
end

function on_midi(e)
  if not is_daw_port(e.port) then return end

  if e.type == 'note_on' and e.value > 0 then
    handle_pad_press(e)
  elseif e.type == 'cc' and e.channel == 16 then
    handle_transport_cc(e)
  end
end
