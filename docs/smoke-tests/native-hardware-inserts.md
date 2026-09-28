# Smoke tests: native hardware inserts (#2279)

Use the native engine with a hardware synth connected by MIDI out and returning
audio into an interface input. Set it up once on an empty MIDI track: add
External Instrument, pick the synth's MIDI port under "MIDI to", its input
under "Return from", and record-arm the track's MIDI input.

## Live

- [ ] Play notes from a keyboard. The synth sounds through the track: its meter
      moves, and fader, pan, mute and the track's effects apply to it.
- [ ] Play a MIDI clip on the track. The synth plays it in time with a drum clip
      on another track. Record a loopback of both and measure the offset; set it
      in "Latency (ms)" and confirm the offset closes.
- [ ] Stop the transport while a long note plays. The note stops.
- [ ] Jump the playhead while notes play. Nothing hangs.
- [ ] Mute the track while a note plays. The note stops.
- [ ] Pick another return input while playing. The synth is heard from the new
      input without restarting the transport.
- [ ] Unarm the track and play the synth's own keyboard: with Local Control on,
      no note doubles. Arm it again and the keyboard reaches the track.
- [ ] Set "MIDI to" to None. The track goes quiet and nothing hangs on the synth.
- [ ] Save, reopen the project, and play: the insert comes back as it was.

## Export

- [ ] Export the song. A progress window plays the range once in real time, then
      the render runs. The file has the synth in time with the rest.
- [ ] Export at a different sample rate from the interface's. The synth is in the
      file, at the right pitch and in time.
- [ ] Cancel during the real-time pass. Nothing is written and nothing hangs.
- [ ] Bounce a clip on the synth track. The bounced audio is the synth's.

These checks supplement the automated live insert, capture pass and render
binding tests; mark them only after running them.
