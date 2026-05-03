-- foot-pedal.lua
-- Sustain pedal (CC #64) toggles transport play / stop on selected track's
-- session clip. Press = launch the selected clip; release = stop the track.

function on_midi(e)
  if e.type ~= 'cc' or e.number ~= 64 then return end

  local track = magda.selection.track()
  if not track then return end

  if e.value >= 64 then
    -- pedal pressed
    local clip = magda.selection.clip()
    if clip then
      magda.session.launch_clip(clip)
    end
  else
    -- pedal released
    magda.session.stop_track(track)
  end
end
