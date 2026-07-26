-- Novation Launchpad MK1 / Launchpad Classic controller script for MAGDA.
--
-- Modes:
--   Session  - 8 tracks x 8 scenes, scene launch buttons, arrow-key banking
--   User 1   - drum-rack note layout routed to the selected MAGDA track
--   User 2   - X/Y note layout routed to the selected MAGDA track
--   Mixer    - 8 vertical volume or pan faders (press Mixer again to toggle)
--
-- Protocol reference: original Launchpad Programmer's Reference.
-- The MK1 X/Y grid uses note = row * 16 + column, with row 0 at the top;
-- the right-side scene buttons are column 8 and top buttons are CC 104..111.

local out = nil
local mode = "session"
local mixer_page = "volume"
local track_offset = 0
local scene_offset = 0
local last_led = {}

local CC_UP = 104
local CC_DOWN = 105
local CC_LEFT = 106
local CC_RIGHT = 107
local CC_SESSION = 108
local CC_USER1 = 109
local CC_USER2 = 110
local CC_MIXER = 111

-- Original Launchpad LED velocities (copy + clear flags included).
local LED_OFF = 12
local LED_RED = 15
local LED_AMBER = 63
local LED_YELLOW = 62
local LED_GREEN_DIM = 28
local LED_GREEN = 60

local function find_output()
  if out then return out end
  local configured = magda.midi.default_output()
  if configured and configured ~= "" then out = configured; return out end
  for _, name in ipairs(magda.midi.outputs()) do
    if name:lower():find("launchpad") then out = name; return out end
  end
  return nil
end

local function send_cc(cc, value)
  local port = find_output()
  if port then magda.midi.send_cc(port, 1, cc, value) end
end

local function send_pad(note, value)
  local port = find_output()
  if not port then return end
  local signature = tostring(value)
  if last_led[note] == signature then return end
  last_led[note] = signature
  magda.midi.send_note_on(port, 1, note, value)
end

local function invalidate_leds()
  last_led = {}
end

local function set_mapping(value)
  -- CC 0: 1 = X/Y layout, 2 = drum-rack layout, 0 = reset.
  send_cc(0, value)
end

local function publish_session_view()
  magda.session.set_view(scene_offset, 8)
end

local function visible_track(column)
  local tracks = magda.tracks.list()
  return tracks[track_offset + column + 1]
end

local function xy_from_note(note)
  local row = math.floor(note / 16)
  local column = note % 16
  if row < 0 or row > 7 or column < 0 or column > 8 then return nil, nil end
  return row, column
end

local function set_top_leds()
  local active = {
    [CC_SESSION] = mode == "session",
    [CC_USER1] = mode == "user1",
    [CC_USER2] = mode == "user2",
    [CC_MIXER] = mode == "mixer",
  }
  send_cc(CC_UP, LED_GREEN_DIM)
  send_cc(CC_DOWN, LED_GREEN_DIM)
  send_cc(CC_LEFT, LED_GREEN_DIM)
  send_cc(CC_RIGHT, LED_GREEN_DIM)
  for cc = CC_SESSION, CC_MIXER do
    send_cc(cc, active[cc] and LED_GREEN or LED_OFF)
  end
end

local function enter_mode(next_mode)
  mode = next_mode
  invalidate_leds()
  if mode == "user1" then
    set_mapping(2)
  else
    set_mapping(1)
  end
  if mode == "session" then publish_session_view() else magda.session.set_view(0, 0) end
  set_top_leds()
end

local function refresh_session()
  for row = 0, 7 do
    local scene = scene_offset + row
    for column = 0, 7 do
      local note = row * 16 + column
      local track = visible_track(column)
      local clip = track and magda.session.clip_in_slot(track.id, scene) or nil
      if not clip then
        send_pad(note, LED_OFF)
      else
        local state = magda.session.clip_play_state(clip)
        if state == "playing" then
          send_pad(note, LED_GREEN)
        elseif state == "queued" then
          send_pad(note, LED_YELLOW)
        else
          send_pad(note, LED_AMBER)
        end
      end
    end
    send_pad(row * 16 + 8, LED_GREEN_DIM)
  end
end

local function mixer_value(track)
  if not track then return 0 end
  if mixer_page == "volume" then return math.max(0, math.min(1, track.volume or 0)) end
  return math.max(0, math.min(1, ((track.pan or 0) + 1) * 0.5))
end

local function refresh_mixer()
  for column = 0, 7 do
    local track = visible_track(column)
    local value = mixer_value(track)
    local lit_rows = math.floor(value * 7 + 0.5)
    for row = 0, 7 do
      local level = 7 - row
      local lit = track and level <= lit_rows
      local colour = mixer_page == "volume" and LED_GREEN or LED_AMBER
      send_pad(row * 16 + column, lit and colour or LED_OFF)
    end
  end
  for row = 0, 7 do
    local value = LED_OFF
    if row == 0 then value = mixer_page == "volume" and LED_GREEN or LED_GREEN_DIM end
    if row == 1 then value = mixer_page == "pan" and LED_AMBER or LED_GREEN_DIM end
    send_pad(row * 16 + 8, value)
  end
end

local function move_bank(cc)
  if cc == CC_UP then
    scene_offset = math.max(0, scene_offset - 1)
  elseif cc == CC_DOWN then
    scene_offset = scene_offset + 1
  elseif cc == CC_LEFT then
    track_offset = math.max(0, track_offset - 1)
  elseif cc == CC_RIGHT then
    track_offset = track_offset + 1
  end
  invalidate_leds()
  if mode == "session" then publish_session_view() end
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
  local row, column = xy_from_note(note)
  if not row then return end
  local scene = scene_offset + row
  if column == 8 then
    magda.session.launch_scene(scene)
    return
  end
  local track = visible_track(column)
  if not track then return end
  local clip = magda.session.clip_in_slot(track.id, scene)
  if clip then magda.session.launch_clip(clip) end
end

local function handle_mixer_press(note)
  local row, column = xy_from_note(note)
  if not row then return end
  if column == 8 then
    if row == 0 then mixer_page = "volume" end
    if row == 1 then mixer_page = "pan" end
    invalidate_leds()
    return
  end
  local track = visible_track(column)
  if not track then return end
  local normalized = (7 - row) / 7
  if mixer_page == "volume" then
    magda.tracks.set_volume(track.id, normalized)
  else
    magda.tracks.set_pan(track.id, normalized * 2 - 1)
  end
  invalidate_leds()
end

function on_load()
  magda.log.info("[launchpad-mk1] loading")
  send_cc(0, 0)
  enter_mode("session")
end

function on_unload()
  magda.session.set_view(0, 0)
  send_cc(0, 0)
end

function on_tick(dt)
  if mode == "session" then refresh_session()
  elseif mode == "mixer" then refresh_mixer() end
end

function on_midi(e)
  if mode == "user1" or mode == "user2" then
    if e.type == "note_on" or e.type == "note_off" then inject_selected(e) end
  elseif e.type == "note_on" and e.value > 0 then
    if mode == "session" then handle_session_press(e.number)
    elseif mode == "mixer" then handle_mixer_press(e.number) end
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
    if mode == "mixer" then
      mixer_page = mixer_page == "volume" and "pan" or "volume"
      invalidate_leds()
    else
      mixer_page = "volume"
      enter_mode("mixer")
    end
  end
end
