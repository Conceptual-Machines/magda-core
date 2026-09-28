# Smoke tests: engine switch (#2579)

Manual verification for audio engine selection. Run after selecting the native engine in
Settings, Audio, then repeat the same checklist under Tracktion.

**Setup**: a project with at least one external plugin.

## 1. Under magda

Log inspection:

- [ ] The log has "[engine] rendering through magda::engine".
- [ ] The log has no "Playback context allocated".
- [ ] The log has no "AudioBridge initialized".

Plugin loading:

- [ ] A project with one VST3 shows one plugin process, not two.
- [ ] The project loads in roughly half the time compared to Tracktion.

Live input and MIDI:

- [ ] Piano roll key on an idle instrument track: sound, track meter, master meter.
- [ ] QWERTY on a track with monitor In: sound. Same track, monitor Off: silent.
- [ ] A hardware keyboard on a track routed to it: sound and the activity light.

Session state:

- [ ] Tempo change moves the ruler and the click together.
- [ ] Loop region loops.
- [ ] Save and reload keeps zoom, scroll and view mode.

## 2. Under Tracktion

Log inspection inverts:

- [ ] The log has "[engine] rendering through Tracktion".
- [ ] The log has "Playback context allocated" and "AudioBridge initialized".

Then repeat the plugin loading, live input and session state sections. Everything behaves as
it did before #2579.
