# Smoke tests: native hardware outputs (#2272)

Use the native engine and an audio interface with at least four enabled output
channels. Start with a stereo clip whose left and right channels are distinct.

- [ ] Route the track to outputs 3–4. Only those outputs carry the clip; the
      master meter and outputs 1–2 remain silent.
- [ ] Add a second track routed to Master. Both destinations play independently.
      Changing the master fader affects only the track routed through Master.
- [ ] Route two tracks to the same hardware pair. Their signals sum once, and
      each track's mute, solo, pan and fader still work.
- [ ] Select a mono output. The clip's left channel reaches that output, matching
      the existing Tracktion routing; no other hardware channel carries it.
- [ ] Change a playing track between Master and another hardware pair. The
      destination updates without requiring a transport restart.
- [ ] Disable an output pair, or switch to an interface without that saved
      destination. Its tracks become silent and a diagnostic names the missing
      output. Restore the original configuration and verify routing recovers.
- [ ] Enable only a later pair, such as 3–4. Its selection still reaches the
      correct physical channels despite the callback having just two channels.
- [ ] Save and reopen the project. Existing mono and `stereo:` output selections
      retain their destinations.
- [ ] Export the project to a stereo file. Hardware-routed tracks are included
      in the master mix, including when their interface is disconnected.

These hardware listening checks supplement the automated compiler, renderer,
channel-map and host callback tests; mark them only after running them.
