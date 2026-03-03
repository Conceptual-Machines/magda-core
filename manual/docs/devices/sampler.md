# Sampler

The Sampler is MAGDA's built-in sample playback instrument with multi-voice support and synthesis controls.

## Overview

Load an audio sample and play it chromatically across the keyboard. The Sampler supports up to 8 simultaneous voices (polyphony).

## Loading Samples

- Drag an audio file from the Media Explorer onto the Sampler device
- Or click the file browser button within the Sampler UI to select a file

## Controls

### Pitch

- **Root note** — The MIDI note at which the sample plays at its original pitch
- **Pitch** — Transpose in semitones
- **Fine-tune** — Detune in cents

### Sample Region

- **Start** — Sample playback start point
- **End** — Sample playback end point
- Drag the start/end markers in the waveform display to define the playback region

### ADSR Envelope

- **Attack** — Time for the sound to reach full level after a note-on
- **Decay** — Time to fall from peak to sustain level
- **Sustain** — Level held while the note is held
- **Release** — Time to fade out after note-off

### Voices

- **Polyphony** — Up to 8 simultaneous voices
- When the voice limit is reached, the oldest note is stolen
