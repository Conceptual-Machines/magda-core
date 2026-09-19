# Native hardware audio input (#2553)

A track whose audio input names a hardware channel hears that channel on the
native engine. The device callback hands its input to the session, and each
track's input op reads the channels its saved name resolves to.

Saved names stay project data. The host resolves them against the current
interface: a channel's wave-in name, `stereo:` plus the name of a pair's first
channel, or the `In N` / `stereo:In N` fallback the input menus store when the
device layer has no name. A wave-in that covers two channels reads both under
its bare name, and `default`, which switching a track's audio input on stores,
reads the menu's first channel option. Physical channels map to packed callback indices, so a pair
still reads the right channels when only some inputs are open. A name the
interface does not have, or an input disabled in Audio Settings, renders silence
and is logged once.

The mapping travels in the live routing snapshot rather than the plan, so a
device restart or an input change reaches the track without a recompile. A
mono input fills both sides of the track.

Monitoring is unchanged: the input gate passes the signal when the track is
armed or set to Monitor In. The input's own meter sits ahead of the gate; a
monitoring track's meter shows the larger of its output and its input.

Input latency calibration and audio takes are later slices of #2553. Opening
only the selected channels is #2588.

## Automated verification

- `test_hardware_input_map`: name resolution, pairs, packing, enablement.
- `test_live_input`: a track's input follows the published channels.
- `test_live_midi_routing`: channels republish the routing snapshot.
- `test_engine_host_audio_input_juce`: the real callback, monitored and
  unmonitored tracks, mono input, a restart that repacks channels, and a
  missing name.

## Hardware checks

- [ ] Select a stereo input pair on an audio track with Monitor In. Both sides
      are audible and the track meter moves.
- [ ] Select a mono input. It is heard in both ears.
- [ ] Monitor Off and unarmed: silent, and the meter stays still. Arm it: the
      input is heard.
- [ ] Disable the selected input in Audio Settings. The track goes silent and
      the log names the input once. Re-enable it and the signal returns.
- [ ] Open a project saved on the Tracktion engine with a named input. The same
      channels are heard on native.
