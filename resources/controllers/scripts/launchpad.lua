-- launchpad.lua
-- 8x8 pad grid → launch first 64 clips of the first 8 tracks (one row each).
-- Side buttons (notes 8/24/40/56/72/88/104/120 in Launchpad's grid) → stop track.
--
-- This script targets the Novation Launchpad's default layout where:
--   - Pad notes start at 11 (bottom-left) and increment by 1 across, +10 up.
--   - The right-side scene-launch column is the 9th cell of each row.
--
-- Adjust note numbers for your specific pad device if needed.

local function pad_to_track_and_index(note)
  -- Launchpad layout: row r (0..7), col c (0..7)
  -- note = 11 + r*10 + c
  local row = math.floor((note - 11) / 10)
  local col = (note - 11) % 10
  if row < 0 or row > 7 or col < 0 or col > 8 then return nil, nil end
  return row + 1, col  -- track 1..8, col 0..7 = clip slot, col 8 = scene/stop
end

function on_midi(e)
  if e.type ~= 'note_on' or e.value == 0 then return end

  local track_id, slot = pad_to_track_and_index(e.number)
  if not track_id or not slot then return end

  if slot == 8 then
    -- Right-side button: stop the track.
    magda.session.stop_track(track_id)
    return
  end

  -- Grid pad: launch the slot-th session clip on this track.
  local clips = magda.clips.list_on_track(track_id)
  local clip = clips[slot + 1]   -- Lua arrays are 1-indexed
  if clip then
    magda.session.launch_clip(clip)
  end
end
