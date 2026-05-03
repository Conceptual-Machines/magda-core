-- 8-knobs.lua
-- CC #1..#8 → volume of tracks 1..8 (0..1 normalized).
-- Drop into ~/Library/Application Support/MAGDA/Scripts/Controllers/

function on_midi(e)
  if e.type ~= 'cc' then return end
  if e.number < 1 or e.number > 8 then return end

  local track_id = e.number
  magda.tracks.set_volume(track_id, e.value / 127.0)
end
