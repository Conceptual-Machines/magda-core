# MAGDA — Product Roadmap

> Multi-Agent Generative Digital Audio — a DAW that combines traditional audio production with AI-driven multi-agent collaboration.

This roadmap organises all open GitHub issues into milestones, starting with the minimum feature set needed for a public **0.1.0** release and progressing through increasingly ambitious capabilities. Each milestone builds on the previous one; issues are grouped by functional domain within each milestone.

---

## Milestone Overview

| Version | Codename | Theme | Target |
|---------|----------|-------|--------|
| **0.1.0** | **Foundation** | First usable release — record, edit, mix, export, with basic AI | MVP |
| **0.2.0** | **Creative Flow** | Enhanced editing, comping, UI polish, new MIDI tools | Near-term |
| **0.3.0** | **Extensibility** | WebSocket API, user-facing DSL, remote controller scripting | Medium-term |
| **0.4.0** | **Power User** | Keybindings, project templates, interop, arrangement sections | Medium-term |
| **0.5.0** | **Studio** | Faust DSP integration, session view, advanced workflows | Long-term |

---

## 0.1.0 — Foundation (MVP)

The goal of 0.1.0 is a **functional, distributable DAW** that a user can install, open, create a project, record audio and MIDI, edit, mix with plugins, and export a final file. The existing AI command system (DSL interpreter, OpenAI integration, agent interface) ships as-is — it's already built and functional, so there's no reason to strip it out. 0.1.0 is about completing the core DAW loop, not adding new AI capabilities.

### Critical Bug Fixes

| # | Title | Priority |
|---|-------|----------|
| 611 | Audio export renders silent file when using test tone generator | **P0** |
| 762 | Piano roll note preview silenced when track input monitoring is Off | **P0** |

### Audio Recording & Playback

| # | Title | Status | Priority |
|---|-------|--------|----------|
| 591 | Audio recording (arrangement and session) | **Done** (needs further testing) | **P0** |
| — | Session recording to arrangement (transfer session clips to arrangement timeline) | Pending | **P1** |

Audio recording is implemented. Remaining work is hardening and edge-case testing. Session-to-arrangement transfer should be straightforward given the existing clip infrastructure.

### MIDI Editor (Core)

| # | Title | Priority |
|---|-------|----------|
| 737 | Add pitchbend and MIDI CC support to MIDIEditor | **P0** |

Pitchbend and MIDI CC editing are standard expectations for any DAW's piano roll. Without them, MIDI editing feels incomplete.

### Mixer & Routing (Core)

| # | Title | Priority |
|---|-------|----------|
| 729 | Implement sends in mixer view | **P0** |
| 765 | Mixer channel strip UI restyling: text sliders for fader and pan | **P1** |

Sends are fundamental to mixing — reverb buses, parallel compression, and headphone mixes all depend on them.

### Plugin System (Core)

VST/AU plugin hosting is already working. The remaining work is completing the custom parameter definition system and adding a built-in utility plugin.

| # | Title | Priority |
|---|-------|----------|
| — | Finish custom parameter definition per plugin (infrastructure exists, needs full implementation) | **P1** |
| 763 | Utility plugin (gain, pan, phase inversion) — UI only, DSP provided by TE's VolumeAndPanPlugin | **P1** |

### Project Format (Core)

These issues establish the foundational project save/load system. Without them, users lose their work.

| # | Title | Priority |
|---|-------|----------|
| 64 | Project file structure (JSON-based) | **P0** |
| 65 | Project metadata | **P0** |
| 66 | Track serialization | **P0** |
| 67 | Clip pool serialization | **P0** |
| 68 | Automation data serialization | **P0** |
| 69 | Plugin state serialization | **P0** |
| 72 | Auto-save system | **P1** |

### Build & Distribution

Without CI and a signed build pipeline, there is no release.

| # | Title | Priority |
|---|-------|----------|
| 734 | Set up CI for macOS and Windows builds | **P0** |
| 735 | Set up release CI: build, sign, and distribute | **P0** |
| 736 | Create product website | **P1** |

### UI & Visual Polish

| # | Title | Priority |
|---|-------|----------|
| 766 | UI font audit and fine-tuning pass | **P1** |
| — | Track headers top area: additional controls (TBD) | **P2** |

### Summary

0.1.0 ships when a user can: install MAGDA from a signed build, create a new project, record audio and MIDI, edit clips and notes in the piano roll (including pitchbend and CC lanes), route sends for effects buses, load VST/AU plugins and built-in effects, mix with volume/pan/mute/solo, use basic AI commands via the existing agent system, save/load their project, and export a stereo audio file. Everything else is a bonus.

**Issue count: 21** (includes 3 new issues to be created)

---

## 0.2.0 — Creative Flow

With the foundation stable, 0.2.0 focuses on the editing and mixing features that make a DAW *enjoyable* to use — take lanes, advanced MIDI tools, mixer polish, and visual refinements.

### MIDI Editor Enhancements

| # | Title | Priority |
|---|-------|----------|
| 738 | Add MPE (MIDI Polyphonic Expression) support to MIDIEditor | **P1** |
| — | Note repeater / arpeggiator | **P1** |
| — | Chord detection (display chords from MIDI input) | **P2** |
| — | Chord suggestion (suggest next chord based on progression) | **P2** |

### Clip & Track Editing

| # | Title | Priority |
|---|-------|----------|
| 662 | Take lanes and comping | **P1** |
| 663 | Clip versioning / history snapshots | **P2** |
| 16 | Internal clip loop region | **P1** |
| 15 | Print to audio preserves MIDI ancestry | **P2** |

### Mixer & Routing

| # | Title | Priority |
|---|-------|----------|
| 730 | Add post-FX section with dedicated bottom panel | **P1** |

### Modulation & Automation

| # | Title | Priority |
|---|-------|----------|
| 590 | Modulation system integration testing (including external triggers) | **P1** |
| 758 | Add modulator and macro support to Drum Grid | **P2** |
| — | Modulation to automation (bake modulation curves into automation lanes) | **P1** |

### UI & Visual Polish

| # | Title | Priority |
|---|-------|----------|
| 756 | Support for Custom Track and Clip Colors | **P2** |
| 761 | Spectrum Analyzer & Oscilloscope plugins | **P2** |
| 739 | Add CPU consumption monitor | **P2** |

### Project Format (Extended)

| # | Title | Priority |
|---|-------|----------|
| 70 | Media management | **P1** |
| 71 | Missing media dialog | **P1** |
| 73 | Undo history persistence | **P2** |

**Issue count: 18** (includes 4 new issues to be created)

---

## 0.3.0 — Extensibility

This milestone opens MAGDA up to external tools and power users. The existing DSL (used internally by the AI agent system) becomes a user-facing scripting language, and a WebSocket server exposes it to external clients — enabling control from any language, remote apps, and custom integrations.

### WebSocket API

| # | Title | Priority |
|---|-------|----------|
| — | WebSocket server for external DSL command execution | **P0** |
| 31 | Unix socket IPC for external agents | **P1** |

The WebSocket server is the primary transport layer for MAGDA's API. External clients (Python scripts, web UIs, phone apps, AI agents) connect and send DSL commands. Responses are JSON. This replaces the previously planned Lua scripting approach — the DSL is already the scripting language, and WebSockets make it language-agnostic.

### User-Facing DSL

| # | Title | Priority |
|---|-------|----------|
| 139 | DSL: Command palette UI | **P0** |
| 140 | DSL: Console panel with REPL | **P0** |
| 137 | DSL: Engine with method registration (expand existing) | **P1** |
| 138 | DSL: MagicaDSL class with Tracktion integration (expand existing) | **P1** |
| 674 | Plugin alias system (@pluginname syntax for AI/DSL) | **P2** |

The DSL command palette and REPL console give users direct access to the same command language the AI agents use. Power users can script complex operations, and the commands are readable enough to learn from watching what the AI generates.

### Remote Controller Scripting

| # | Title | Priority |
|---|-------|----------|
| 592 | MIDI controller scripting | **P1** |
| — | Remote controller scripting (map hardware controllers to DSL commands via WebSocket) | **P1** |

With the WebSocket API in place, hardware controllers (MIDI, OSC-capable devices, Stream Deck, etc.) can be mapped to DSL commands through lightweight bridge scripts. This also enables mobile controller apps.

### Agent Framework (Expansion)

| # | Title | Priority |
|---|-------|----------|
| 27 | Agent interface definition (formalise existing) | **P1** |
| 33 | Agent sandboxing | **P2** |

**Issue count: 11** (includes 2 new issues to be created)

---

## 0.4.0 — Power User

Customisation, templates, arrangement tools, and interoperability features that make MAGDA feel like home for daily use.

### Arrangement

| # | Title | Priority |
|---|-------|----------|
| — | Arrangement sections (verse, chorus, bridge markers with section-level operations) | **P1** |

Arrangement sections let users label and manipulate song structure at a high level — select a section and move, duplicate, or delete it as a unit. Essential for composition workflows.

### Keybinding & Command System

| # | Title | Priority |
|---|-------|----------|
| 22 | User keybinding configuration file | **P1** |
| 23 | Keybinding import/export | **P2** |
| 24 | Built-in keybinding presets | **P2** |
| 25 | Context-aware commands | **P1** |

### Project Management

| # | Title | Priority |
|---|-------|----------|
| 185 | Project templates system | **P1** |
| 74 | DAWproject import/export | **P2** |

### Plugin System

| # | Title | Priority |
|---|-------|----------|
| 80 | Plugin hosting abstraction | **P2** |
| 596 | Scripting API for custom device UIs (theming/skinning) | **P3** |

**Issue count: 9** (includes 1 new issue to be created)

---

## 0.5.0 — Studio

Faust DSP integration, session view, and advanced workflows that push MAGDA into professional territory.

### Faust DSP Integration

See [FAUST_INTEGRATION.md](FAUST_INTEGRATION.md) for the full design document. Faust devices compile externally to dynamic libraries and load into MAGDA's signal chain at runtime — no LLVM dependency in the MAGDA binary.

| # | Title | Priority |
|---|-------|----------|
| 107 | Faust compiler integration (external .dsp → .dylib pipeline) | **P1** |
| 683 | Faust DSP integration for custom effects/instruments | **P1** |
| 108 | DSP script editor (with Faust syntax support) | **P1** |
| 110 | DSP parameter discovery (auto-generate UI from Faust metadata) | **P1** |
| 109 | DSP hot-reload (crossfade to recompiled library) | **P2** |
| 111 | DSP script versioning | **P2** |

### Session View (Foundation)

| # | Title | Priority |
|---|-------|----------|
| 587 | Clip follow actions (play next, stop, etc.) | **P1** |
| 682 | Session view cross-arrangement editing and clip operations | **P1** |

### OSC Support (Optional)

| # | Title | Priority |
|---|-------|----------|
| — | OSC input/output for real-time parameter control | **P2** |

OSC complements the WebSocket API for real-time continuous control — faders, knobs, sensor data. Lower priority than WebSocket since it serves a narrower use case, but valuable for interop with TouchOSC, Max/MSP, SuperCollider, and similar tools.

**Issue count: 10** (includes 1 new issue to be created)

---

## Paid Features (Not in v0.x)

The following features are planned as premium/paid additions and are not part of the open-source v0.x roadmap:

**Project Version Control & Creative Branching** — Named versions, creative branching, visual diff, selective revert, branch merging, delta compression for audio history. Issues: #127, #128, #129, #130, #131, #132, #133, #134.

**Voice Command System** — Hands-free DAW control via speech recognition, wake word detection, transport/track/mixing voice commands, voice filtering from recordings, foot pedal activation. Issues: #100, #101, #102, #103, #104, #105.

---

## Issue Categories (Cross-Reference)

For quick filtering, every issue falls into one of these domains:

| Category | Issues |
|----------|--------|
| **Bug** | #611, #762 |
| **Audio / Recording** | #591, session-to-arrangement |
| **MIDI Editor** | #737, #738, note repeater, chord detection, chord suggestion |
| **Clip & Track Editing** | #15, #16, #662, #663, #756 |
| **Arrangement** | arrangement sections |
| **Mixer & Routing** | #729, #730 |
| **UI & Visual Polish (0.1.0)** | #765, #766, track headers TBD |
| **Plugin System** | #80, #596, #763, custom parameter definitions |
| **Monitoring & Analysis** | #739, #761 |
| **Modulation / Automation** | #590, #758, modulation-to-automation |
| **Project Format** | #64, #65, #66, #67, #68, #69, #70, #71, #72, #73, #185 |
| **Interoperability** | #74 |
| **Build / CI / Website** | #734, #735, #736 |
| **API / WebSocket** | WebSocket server, #31 |
| **DSL (MAGICA)** | #137, #138, #139, #140, #674 |
| **Controller Scripting** | #592, remote controller scripting |
| **Agent Framework** | #27, #33 |
| **Faust / DSP** | #107, #108, #109, #110, #111, #683 |
| **Session View** | #587, #682 |
| **OSC** | OSC support |

---

## Suggested Development Order Within 0.1.0

For the MVP, here is the recommended implementation sequence:

1. ~~**Audio recording** (#591) — **Done**, needs further testing~~
2. **Project file structure** (#64, #65) — establish the save format first so everything built after can be persisted
3. **Track & clip serialization** (#66, #67) — tracks and clips are the core data model
4. **Automation & plugin state serialization** (#68, #69) — complete the serialization layer
5. **Session recording to arrangement** — transfer session clips to the timeline
6. **Custom parameter definitions** — complete the existing infrastructure
7. **Pitchbend & MIDI CC lanes** (#737) — complete the MIDI editing experience
8. **Mixer sends** (#729) — essential for any real mixing workflow
9. **Bug fixes** (#611, #762) — fix export and piano roll bugs
10. **Auto-save** (#72) — safety net for users
11. **CI setup** (#734) — automated builds for both platforms
12. **Release CI** (#735) — signed, distributable builds
13. **Website** (#736) — landing page for the release

---

## Notes

- **Issue coverage**: This roadmap covers the major tracked issues plus several new items identified during planning. Items marked with "—" need GitHub issues created.
- **Priority scale**: P0 = must have for this milestone, P1 = should have, P2 = nice to have, P3 = stretch goal.
- **AI ships from day one**: The existing agent system (DSL interpreter, OpenAI integration, basic command interface) is already functional and ships with 0.1.0. Milestone 0.3.0 expands this with WebSocket API and user-facing DSL tools.
- **VST/AU plugins already work**: Plugin hosting via Tracktion Engine is functional. The remaining plugin work is completing custom parameter definitions (0.1.0) and plugin hosting abstraction (0.4.0).
- **API strategy**: WebSocket + DSL is the primary extensibility layer (0.3.0). OSC is a complementary real-time control protocol (0.5.0). Lua scripting has been dropped in favour of this approach — the DSL already serves as the scripting language.
- **Milestone boundaries are flexible**: If an issue turns out to be trivial, pull it forward. If it's more complex than expected, push it back. The milestone themes matter more than the exact issue assignments.
