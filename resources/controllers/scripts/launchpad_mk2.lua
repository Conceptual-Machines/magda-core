-- Novation Launchpad MK2 controller script for MAGDA.
--
-- Modes:
--   Session  - 8 tracks x 8 scenes, scene launch buttons, arrow-key banking
--   User 1   - Launchpad drum layout routed to the selected MAGDA track
--   User 2   - Launchpad User 2 layout routed to the selected MAGDA track
--   Mixer    - native Launchpad virtual faders; press Mixer again for pan
--
-- Protocol reference: Launchpad MK2 Programmer's Reference Manual.

local out = nil
local mode = "session"
local mixer_page = "volume"
local track_offset = 0
local scene_offset = 0
local last_led = {}
local last_fader = {}

local CC_UP = 104
local CC_DOWN = 105
local CC_LEFT = 106
local CC_RIGHT = 107
local CC_SESSION = 108
local CC_USER1 = 109
local CC_USER2 = 110
local CC_MIXER = 111

local SYSEX = {0x00, 0x20, 0x29, 0x02, 0x18}
local LAYOUT_SESSION = 0
local LAYOUT_USER1 = 1
local LAYOUT_USER2 = 2
local LAYOUT_VOLUME = 4
local LAYOUT_PAN = 5

local function find_output()
  if out then return out end
  local configured = magda.midi.default_output()
  if configured and configured ~= "" then out = configured; return out end
  for _, name in ipairs(magda.midi.outputs()) do
    if name:lower():find("launchpad") then out = name; return out end
  end
  return nil
end

local function sysex(command, values)
  local port = find_output()
  if not port then return end
  local payload = {SYSEX[1], SYSEX[2], SYSEX[3], SYSEX[4], SYSEX[5], command}
  for _, value in ipairs(values or {}) do table.insert(payload, value) end
  magda.midi.send_sysex(port, payload)
end

local function set_layout(layout)
  sysex(0x22, {layout})
end

local function publish_session_view()
  magda.session.set_view(scene_offset, 8)
end

local function visible_track(column)
  local tracks = magda.tracks.list()
  return tracks[track_offset + column + 1]
end

-- Session layout: 11 is bottom-left, 81 is top-left, 19..89 are side buttons.
local function session_xy(note)
  local tens = math.floor(note / 10)
  local ones = note % 10
  if tens < 1 or tens > 8 or ones < 1 or ones > 9 then return nil, nil end
  return 8 - tens, ones - 1 -- row from top, column from left
end

local function note_for(row, column)
  return (8 - row) * 10 + column + 1
end

local function invalidate_leds()
  last_led = {}
  last_fader = {}
end

local function send_palette(note, channel, colour)
  local port = find_output()
  if not port then return end
  local signature = string.format("p:%d:%d", channel, colour)
  if last_led[note] == signature then return end
  last_led[note] = signature
  magda.midi.send_note_on(port, channel, note, colour)
end

local function send_rgb(note, r, g, b)
  local signature = string.format("r:%d:%d:%d", r, g, b)
  if last_led[note] == signature then return end
  last_led[note] = signature
  sysex(0x0B, {note, r, g, b})
end

local function set_top_leds()
  local port = find_output()
  if not port then return end
  local active = {
    [CC_SESSION] = mode == "session",
    [CC_USER1] = mode == "user1",
    [CC_USER2] = mode == "user2",
    [CC_MIXER] = mode == "mixer",
  }
  for cc = CC_UP, CC_RIGHT do magda.midi.send_cc(port, 1, cc, 45) end
  for cc = CC_SESSION, CC_MIXER do
    magda.midi.send_cc(port, 1, cc, active[cc] and 21 or 0)
  end
end

local function fader_value(track)
  if not track then return 0 end
  if mixer_page == "volume" then
    return math.floor(math.max(0, math.min(1, track.volume or 0)) * 127 + 0.5)
  end
  return math.floor(math.max(0, math.min(1, ((track.pan or 0) + 1) * 0.5)) * 127 + 0.5)
end

local function initialise_faders()
  local fader_type = mixer_page == "volume" and 0 or 1
  local colour = mixer_page == "volume" and 21 or 13
  for column = 0, 7 do
    local value = fader_value(visible_track(column))
    sysex(0x2B, {column, fader_type, colour, value})
    last_fader[column] = value
  end
end

local function enter_mode(next_mode)
  mode = next_mode
  invalidate_leds()
  if mode == "session" then
    set_layout(LAYOUT_SESSION)
    publish_session_view()
  elseif mode == "user1" then
    set_layout(LAYOUT_USER1)
    magda.session.set_view(0, 0)
  elseif mode == "user2" then
    set_layout(LAYOUT_USER2)
    magda.session.set_view(0, 0)
  else
    set_layout(mixer_page == "volume" and LAYOUT_VOLUME or LAYOUT_PAN)
    magda.session.set_view(0, 0)
    initialise_faders()
  end
  set_top_leds()
end

local function refresh_session()
  for row = 0, 7 do
    local scene = scene_offset + row
    for column = 0, 7 do
      local note = note_for(row, column)
      local track = visible_track(column)
      local clip = track and magda.session.clip_in_slot(track.id, scene) or nil
      if not clip then
        send_palette(note, 1, 0)
      else
        local state = magda.session.clip_play_state(clip)
        if state == "playing" then
          send_palette(note, 3, 21)
        elseif state == "queued" then
          send_palette(note, 2, 13)
        else
          local rgb = magda.clips.colour(clip)
          if rgb then send_rgb(note, rgb.r, rgb.g, rgb.b)
          else send_palette(note, 1, 45) end
        end
      end
    end
    send_palette(note_for(row, 8), 1, 21)
  end
end

local function refresh_faders()
  local port = find_output()
  if not port then return end
  for column = 0, 7 do
    local value = fader_value(visible_track(column))
    if last_fader[column] ~= value then
      last_fader[column] = value
      magda.midi.send_cc(port, 1, 21 + column, value)
    end
  end
end

local function move_bank(cc)
  if cc == CC_UP then scene_offset = math.max(0, scene_offset - 1)
  elseif cc == CC_DOWN then scene_offset = scene_offset + 1
  elseif cc == CC_LEFT then track_offset = math.max(0, track_offset - 1)
  elseif cc == CC_RIGHT then track_offset = track_offset + 1 end
  invalidate_leds()
  if mode == "session" then publish_session_view()
  elseif mode == "mixer" then initialise_faders() end
end

local function inject_selected(e)
  local track = magda.selection.track()
  if not track then return end
  if e.type == "note_on" and e.value > 0 then
    magda.midi.inject_note_on(track, e.channel, e.number, e.value)
  elseif e.type == "note_off" or (e.type == "note_on" and e.value == 0) then
    magda.midi.inject_note_off(track, e.channel, e.number, e.value or 0)
  end
end

local function handle_session_press(note)
  local row, column = session_xy(note)
  if not row then return end
  local scene = scene_offset + row
  if column == 8 then magda.session.launch_scene(scene); return end
  local track = visible_track(column)
  if not track then return end
  local clip = magda.session.clip_in_slot(track.id, scene)
  if clip then magda.session.launch_clip(clip) end
end

local function handle_fader(e)
  if e.number < 21 or e.number > 28 then return false end
  local track = visible_track(e.number - 21)
  if not track then return true end
  if mixer_page == "volume" then
    magda.tracks.set_volume(track.id, e.value / 127)
  else
    magda.tracks.set_pan(track.id, e.value / 127 * 2 - 1)
  end
  last_fader[e.number - 21] = e.value
  return true
end

function on_load()
  magda.log.info("[launchpad-mk2] loading")
  enter_mode("session")
end

function on_unload()
  magda.session.set_view(0, 0)
  set_layout(LAYOUT_SESSION)
  local port = find_output()
  if port then
    for row = 0, 7 do
      for column = 0, 8 do magda.midi.send_note_on(port, 1, note_for(row, column), 0) end
    end
    for cc = CC_UP, CC_MIXER do magda.midi.send_cc(port, 1, cc, 0) end
  end
end

function on_tick(dt)
  if mode == "session" then refresh_session()
  elseif mode == "mixer" then refresh_faders() end
end

function on_midi(e)
  if mode == "user1" or mode == "user2" then
    if e.type == "note_on" or e.type == "note_off" then inject_selected(e) end
  elseif mode == "session" and e.type == "note_on" and e.value > 0 then
    handle_session_press(e.number)
  elseif mode == "mixer" and e.type == "cc" and handle_fader(e) then
    return
  end

  if e.type ~= "cc" or e.value == 0 then return end
  if e.number >= CC_UP and e.number <= CC_RIGHT then
    move_bank(e.number)
  elseif e.number == CC_SESSION then
    enter_mode("session")
  elseif e.number == CC_USER1 then
    enter_mode("user1")
  elseif e.number == CC_USER2 then
    enter_mode("user2")
  elseif e.number == CC_MIXER then
    if mode == "mixer" then mixer_page = mixer_page == "volume" and "pan" or "volume"
    else mixer_page = "volume" end
    enter_mode("mixer")
  end
end
