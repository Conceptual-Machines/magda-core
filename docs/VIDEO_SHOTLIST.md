# MAGDA — Video Showcase Shot List

A series of short YouTube videos showcasing MAGDA's features. Each video is a focused screen recording with voiceover. No fancy editing — just clear, direct demonstrations.

---

## General Guidelines

- **Format**: Screen recording at native resolution + voiceover
- **Length**: 2-4 minutes each (shorter is better)
- **Tone**: Casual, developer-honest. "Here's what this does" not "revolutionary AI-powered workflow"
- **Audio**: Use simple sounds/presets that demonstrate the feature clearly. No need for polished tracks
- **Intro**: Brief title card (MAGDA logo + video title), 2-3 seconds max
- **Outro**: URL to website/repo, no "like and subscribe" fluff

---

## Video 1: UI Overview & Navigation

**Goal**: Orient the viewer. After this video they should understand MAGDA's layout and how to get around.

**Duration**: ~3 minutes

| Timecode | Show | Say (rough notes) |
|----------|------|-------------------|
| 0:00 | Title card | — |
| 0:03 | Full app window, empty project | "This is MAGDA — let me walk you through the interface" |
| 0:10 | Point out the main areas: track headers (left), timeline (centre), toolbar (top) | Explain the three-panel layout |
| 0:30 | Bottom panel — show it switching between content view, chain view, inspector | "The bottom panel is context-sensitive — it changes based on what you have selected" |
| 0:50 | Zoom and scroll the timeline, use the zoom scrollbar | Show navigation feels smooth |
| 1:00 | Switch between arrangement view and session view | "MAGDA has two modes: arrangement for linear composition, session for clip-based workflows" |
| 1:20 | Show the session view grid briefly | Point out clip slots, launch buttons |
| 1:40 | Switch back to arrangement | — |
| 1:50 | Show selection — click a track, click a clip, show how bottom panel responds | "Select a track and the bottom panel shows the device chain. Select a clip and you get the clip editor" |
| 2:10 | Show collapsing/expanding panels, resizing | "Everything is resizable and collapsible" |
| 2:30 | Quick flash of the command palette / agent prompt | "And there's a built-in AI command system — but we'll cover that in a dedicated video" |
| 2:40 | End card | — |

---

## Video 2: Tracks & Track Creation

**Goal**: Show the different ways to create and manage tracks, and what track types are available.

**Duration**: ~2-3 minutes

| Timecode | Show | Say (rough notes) |
|----------|------|-------------------|
| 0:00 | Title card | — |
| 0:03 | Empty project | "Let's look at how tracks work in MAGDA" |
| 0:10 | Create a track via the add track button — show the menu/options | "You can create tracks from the toolbar..." |
| 0:25 | Create a track via right-click context menu on the track area | "...or right-click in the track area" |
| 0:35 | Create a track via drag and drop (if supported — e.g., drag a sample onto the timeline) | "...or drag audio or samples straight onto the timeline" |
| 0:50 | Show track types: audio, MIDI, group | Explain hybrid tracks if applicable |
| 1:10 | Rename a track, change its colour | Quick visual customisation |
| 1:20 | Show group tracks — create a group, drag tracks into it, collapse/expand | "Group tracks let you organise and process multiple tracks together" |
| 1:40 | Show track controls: volume, pan, mute, solo, arm | Point out the track header controls |
| 2:00 | Show input routing — select audio input, MIDI input | "Each track has configurable input routing" |
| 2:15 | Show track reordering by drag | — |
| 2:25 | End card | — |

---

## Video 3: Devices & The Chain Panel

**Goal**: Show how the effects/instrument chain works, how to add and manage devices.

**Duration**: ~3 minutes

| Timecode | Show | Say (rough notes) |
|----------|------|-------------------|
| 0:00 | Title card | — |
| 0:03 | Select a track, show the chain panel in the bottom area | "Every track in MAGDA has a device chain" |
| 0:15 | Add a built-in instrument (Tone Generator or Sampler) | "Let's start with one of MAGDA's built-in instruments" |
| 0:30 | Play a few notes, show it sounds | — |
| 0:40 | Add a built-in effect — EQ, then a compressor | "Add effects by clicking the plus button in the chain" |
| 0:55 | Show the chain visually — devices in series, signal flow left to right | "Signal flows left to right through the chain" |
| 1:10 | Open a built-in device UI — show the EQ controls, tweak some parameters | Show the custom UI components |
| 1:30 | Load a VST/AU plugin — show the plugin browser/scanner | "MAGDA hosts VST and AU plugins" |
| 1:50 | Open the plugin's UI window | Show external plugin GUI |
| 2:00 | Bypass a device, reorder devices by drag | "You can bypass or reorder devices in the chain" |
| 2:15 | Show the Magda Sampler briefly — load a sample, play it | Quick taste of the sampler |
| 2:30 | Show parameter automation preview — "we'll cover automation in detail later" | Tease future video |
| 2:40 | End card | — |

---

## Video 4: Piano Roll & Audio Editing

**Goal**: Show MIDI editing in the piano roll and basic audio clip operations.

**Duration**: ~3 minutes

| Timecode | Show | Say (rough notes) |
|----------|------|-------------------|
| 0:00 | Title card | — |
| 0:03 | Create a MIDI clip on a track with an instrument loaded | "Let's look at MIDI editing" |
| 0:10 | Double-click to open the piano roll | — |
| 0:15 | Draw in some notes — show the pencil/draw tool | Show note creation |
| 0:30 | Select notes, move them, resize them | "Standard selection tools — move, resize, duplicate" |
| 0:45 | Show snap/quantize — toggle grid, change grid resolution | "Snap to grid with adjustable resolution" |
| 1:00 | Edit velocity — show the velocity lane at the bottom | "Velocity editing in the lane below" |
| 1:15 | Select multiple notes, transpose them | — |
| 1:30 | Play back the result — let the MIDI play through the instrument | Let it sound for a few bars |
| 1:45 | Switch to an audio clip — show waveform display | "Audio clips show waveforms on the timeline" |
| 2:00 | Trim, split, move an audio clip | Basic clip operations |
| 2:15 | Show clip stretching if available | — |
| 2:30 | End card | — |

---

## Video 5: Drum Grid

**Goal**: Show the Drum Grid device — building a beat from scratch.

**Duration**: ~2-3 minutes

| Timecode | Show | Say (rough notes) |
|----------|------|-------------------|
| 0:00 | Title card | — |
| 0:03 | Create a track, add the Drum Grid device | "The Drum Grid is MAGDA's built-in beat sequencer" |
| 0:15 | Show the grid UI — rows are instruments, columns are steps | Orient the viewer |
| 0:25 | Click in a kick pattern — quarter notes | Start simple, let it play |
| 0:35 | Add snare on beats 2 and 4 | Building up |
| 0:45 | Add hi-hats — show eighth notes or sixteenth notes | — |
| 0:55 | Add a percussion element — shaker, clap, etc. | — |
| 1:05 | Show velocity per step if available | "You can set velocity per step" |
| 1:15 | Change the pattern length or time division if available | — |
| 1:25 | Show loading different samples into the grid slots | "Each slot can load any sample" |
| 1:40 | Tweak device parameters — pitch, decay, etc. | Shape the sound |
| 1:55 | Let the full beat play for a few bars | Let it breathe |
| 2:10 | End card | — |

---

## Video 6: Modulation System

**Goal**: Show LFOs and modulators driving parameters in real time.

**Duration**: ~2-3 minutes

| Timecode | Show | Say (rough notes) |
|----------|------|-------------------|
| 0:00 | Title card | — |
| 0:03 | Start with a track playing a sustained sound through a filter | Set the stage |
| 0:10 | Open the modulation panel / add an LFO modulator | "Let's add a modulator" |
| 0:20 | Map the LFO to filter cutoff — show the mapping UI | "Drag the modulation source to any parameter" |
| 0:30 | Play — show the cutoff visually moving and hear the wobble | This is the money shot — visual + audible |
| 0:45 | Change LFO rate — slow it down, speed it up | Show immediate audible change |
| 0:55 | Change LFO shape — sine, triangle, square, etc. | "Different shapes give different character" |
| 1:10 | Adjust modulation depth | — |
| 1:20 | Map the same LFO to a second parameter (e.g., resonance or volume) | "One modulator can drive multiple targets" |
| 1:35 | Add a second modulator — different rate, different target | Show stacking |
| 1:50 | Let the fully modulated sound play — show all the parameters dancing | Visual impact |
| 2:10 | End card | — |

---

## Video 7: Session View

**Goal**: Show the session/clip grid view and how it differs from arrangement.

**Duration**: ~2-3 minutes

| Timecode | Show | Say (rough notes) |
|----------|------|-------------------|
| 0:00 | Title card | — |
| 0:03 | Switch to session view | "Session view is for non-linear, clip-based workflows" |
| 0:10 | Show the grid — tracks as columns, clip slots as rows | Orient the viewer |
| 0:20 | Create a few clips in different slots | — |
| 0:30 | Launch a clip — show it playing | "Click to launch a clip" |
| 0:40 | Launch clips on different tracks — build up layers | Show the live performance feel |
| 0:55 | Stop a clip, swap to a different clip on the same track | — |
| 1:10 | Show scene launching if available (trigger a full row) | "Scenes let you launch entire rows at once" |
| 1:25 | Show recording into a session slot | — |
| 1:40 | Brief mention of session-to-arrangement transfer | "You can capture your session performance into the arrangement" |
| 1:55 | End card | — |

---

## Video 8: Mixer

**Goal**: Show the mixer view and mixing workflow.

**Duration**: ~2-3 minutes

| Timecode | Show | Say (rough notes) |
|----------|------|-------------------|
| 0:00 | Title card | — |
| 0:03 | Open a project with several tracks already populated | Start with content |
| 0:10 | Show the mixer view — channel strips, faders, pan, meters | "The mixer gives you a traditional console view" |
| 0:25 | Adjust faders on a few tracks — show levels changing | — |
| 0:35 | Pan a few elements | — |
| 0:45 | Mute/solo tracks | — |
| 0:55 | Show input routing selector | "Each channel has configurable input and output routing" |
| 1:10 | Show sends (if implemented by recording time) | "Sends for effects buses and parallel processing" |
| 1:25 | Show the master channel strip | — |
| 1:35 | Show meters reacting to playback | Visual feedback |
| 1:50 | End card | — |

---

## Video 9: AI Commands & DSL

**Goal**: Show MAGDA's AI-native features — the command interface and DSL scripting.

**Duration**: ~3-4 minutes (this one deserves more time)

| Timecode | Show | Say (rough notes) |
|----------|------|-------------------|
| 0:00 | Title card | — |
| 0:03 | Open a project with a few tracks | — |
| 0:10 | Open the command interface / agent prompt | "MAGDA has a built-in AI command system" |
| 0:20 | Type a natural language command — e.g., "create a new track called Bass" | Show the AI interpreting and executing |
| 0:35 | Show the DSL command that was generated | "Under the hood, commands are translated to MAGDA's DSL" |
| 0:45 | Type a DSL command directly — e.g., `track("Bass").add_fx(type="reverb")` | "Power users can write DSL commands directly" |
| 1:00 | Show a more complex operation — e.g., mute all tracks, then solo one | — |
| 1:15 | Show batch operations if available | "The DSL can operate on multiple tracks at once" |
| 1:30 | Show the AI suggesting or completing something context-aware | If available — this is the wow moment |
| 1:50 | Open the console/REPL if available | "There's also a scripting console for multi-line operations" |
| 2:10 | Show a practical use case — "set up a basic mixing template" via commands | Real-world utility |
| 2:40 | "The same DSL is what external tools and controllers will use to talk to MAGDA" | Tease the extensibility story |
| 3:00 | End card | — |

---

## Recording Checklist

Before each recording session:

- [ ] Clean desktop — hide notifications, close other apps
- [ ] Set MAGDA to a clean state (or prepared project depending on video)
- [ ] Check audio output is routed correctly for screen recording
- [ ] Test that screen recorder captures both system audio and mic
- [ ] Have a few presets/samples loaded so demos sound decent without setup time
- [ ] Do one dry run to check timing and flow before recording

---

## Publishing Order

Recommended release order (one per week or a batch):

1. **UI Overview** — first impression, orients new viewers
2. **Tracks & Track Creation** — natural next question after seeing the UI
3. **Devices & Chain Panel** — how sound is shaped
4. **Piano Roll & Audio Editing** — core editing workflow
5. **Drum Grid** — fun, visual, good engagement
6. **Modulation System** — impressive visuals, shows depth
7. **Session View** — different workflow mode
8. **Mixer** — mixing workflow
9. **AI Commands & DSL** — the differentiator, saved for last to build anticipation
