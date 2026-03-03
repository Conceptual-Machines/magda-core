# Drum Grid

The Drum Grid is MAGDA's chain-based drum machine. Each pad is a full audio chain, giving you independent processing per drum voice.

## Overview

The Drum Grid maps incoming MIDI notes to individual pads. Each pad has its own sample, FX chain, and mixer controls, making it a self-contained drum kit in a single device.

## Pads

- Each pad is mapped to a specific MIDI note
- Pads can be triggered from the Piano Roll, the Drum Grid Editor (bottom panel), or a MIDI controller
- Click a pad to select it and view its chain in the Inspector

## Per-Pad FX Chain

Each pad has its own FX chain, just like a regular track. You can add plugins, built-in devices, or racks to any pad's chain. This allows effects like distortion on a snare without affecting the kick.

## Per-Pad Mix

Each pad has independent mix controls:

- **Volume** — Pad output level
- **Pan** — Stereo position
- **Output routing** — Route individual pads to separate mixer channels for independent mixing

When a DrumGrid track is expanded in the [Mixer View](../mixer-view.md), each pad appears as its own sub-channel strip.

## Adding Samples

- Drag audio files from the Media Explorer onto a pad to load a sample
- Each pad plays one sample at a time

## MIDI Mapping

The default mapping assigns pads to General MIDI drum note numbers (kick = C1, snare = D1, etc.), but pads can be remapped to any MIDI note.
