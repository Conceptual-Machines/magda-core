# Drum Grid

The Drum Grid is MAGDA's chain-based drum machine. Each pad is a full audio chain, giving you independent processing per drum voice.

![Drum Grid device with macros panel and the pad grid showing two pages of pads](../assets/images/devices/drum-grid.png)

## Overview

The Drum Grid maps incoming MIDI notes to individual pads. Each pad has its own sample, FX chain, and mixer controls, making it a self-contained drum kit in a single device.

## Pads

- Each pad is mapped to a specific MIDI note
- Pads can be triggered from the Piano Roll, the Drum Grid Editor (bottom panel), or a MIDI controller
- Click a pad to select it and view its chain in the Inspector

## Row Labels and Roles

Each pad row can carry a custom name and an instrument **role**. The role is a semantic label (what the pad *is*: a kick, a snare, a closed hat) and shows as a small coloured pill next to the row, abbreviated to a short tag like `K`, `S`, or `HH`.

Right-click a row to set both:

- **Set instrument** - choose a role from the submenu, or **None** to clear it.
- **Rename label...** - give the row a custom name.
- **Clear label** - remove the custom name.
- **Clear role** - remove the role (leaving the pad unlabelled).

Roles are also what the [Drummer agent](../panels/ai-assistant.md#drummer-agent) reads and writes, so labelling your pads lets the AI place hits on the right voices.

The available roles and their short tags:

| Role | Tag | Role | Tag |
|------|-----|------|-----|
| Kick | K | Ride | R |
| Snare | S | Ride Bell | RB |
| Snare Rim | SR | Crash | CR |
| Clap | C | Tom High | TH |
| Closed Hat | HH | Tom Mid | TM |
| Open Hat | OH | Tom Low | TL |
| Pedal Hat | PH | Perc 1-4 | P1-P4 |

## Drumkits

A **drumkit** saves a grid's row layout - each pad's note, custom label, and role - so you can reuse it across projects. Right-click a row to manage kits:

- **Save as drumkit...** - name and save the current grid's row layout.
- **Apply template** - apply a built-in template or one of your saved drumkits to the current grid.

Drumkits store pad metadata (notes, labels, roles), not the samples themselves.

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

### Multi-Sample Drop

Select several samples in the [Media Explorer](../panels/browsers.md) or [Media Library](../panels/media-library.md) and drag them onto any pad. Samples fill consecutive pads in selection order, starting from the drop target. While dragging, every pad that will receive a sample lights up so you can see the range before releasing.

## Pad Envelope and Macros

Each pad plays its sample through MAGDA's sampler, which has an amplitude envelope you can shape per pad:

- **Attack** (ATK), **Decay** (DEC), **Sustain** (SUS), **Release** (REL)

These ADSR controls are full modulation targets. You can link them to the Drum Grid's [macros](../modulation/macros.md) and modulate them with LFOs or envelopes the same way as any other parameter (see [Linking Parameters](../modulation/linking.md)). Slicing a sample across several pads gives each slice its own envelope, so a single macro can, for example, tighten every slice's release at once.

## MIDI Mapping

The default mapping assigns pads to General MIDI drum note numbers (kick = C1, snare = D1, etc.), but pads can be remapped to any MIDI note.
