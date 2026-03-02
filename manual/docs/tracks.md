# Tracks

Tracks are the fundamental building blocks in MAGDA. They appear in all three views — as columns in Session View, rows in Arrangement View, and channel strips in Mixer View.

## Hybrid Track System

MAGDA uses a **hybrid track system**: there is no strict distinction between audio and MIDI tracks. Any track can contain any combination of audio clips, MIDI clips, and other clip types. The track's behavior adapts based on the clips and devices it contains.

## Track Types

| Type | Description |
|------|-------------|
| **Audio** | Standard track for audio recording and playback |
| **Instrument** | Track with a virtual instrument plugin loaded |
| **MIDI** | Track that sends MIDI to external devices |
| **Group** | Bus track that groups multiple child tracks |
| **Aux** | Auxiliary/send-return track for shared effects |
| **Master** | Final stereo output — one per project |

## Track Controls

Every track provides the following controls (visible in track headers and channel strips):

- **Volume fader** — Adjust the track's output level
- **Pan knob** — Position in the stereo field
- **Mute** (M) — Silence the track. Shortcut: select the track and press ++m++
- **Solo** (S) — Solo the track. Shortcut: select the track and press ++shift+s++
- **Record arm** (R) — Arm the track for recording
- **Input monitor** — Monitor the live input signal through the track

## Adding and Managing Tracks

- Press ++ctrl+t++ (++cmd+t++ on macOS) to add a new track
- Right-click a track header for options: rename, color, duplicate, delete, freeze
- Click the track name to rename it

## FX Chain

Each track has an **FX chain** — an ordered list of audio processors applied to the track's signal. The chain can contain:

- **Plugins** — VST3, AU, or VST effect and instrument plugins
- **Built-in devices** — MAGDA's own processors (see [Built-in Devices](devices/built-in.md))
- **Racks** — Container devices with nested chains and parallel routing

### Racks

A rack is a container that holds one or more parallel chains. Signal flows into the rack, splits across chains, and mixes back together at the output. Use racks for:

- Parallel processing (e.g., dry/wet blends)
- Complex multi-band setups
- Organized device grouping

## Freeze and Bounce

- **Freeze** — Render the track's output to a temporary audio file to free up CPU. The track becomes read-only until unfrozen. Use **Track > Freeze Track**.
- **Bounce in Place** — Render the track to a new audio clip that replaces the original content. Use **Track > Bounce in Place**.
