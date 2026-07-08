# External FX & External Instrument — Integration Plan

Status: design, pre-implementation. Branch: `feat/external-instrument-fx`.

Goal: let a track route audio/MIDI out to outboard hardware and back, for two
user-facing features:

- **External FX** — an in-chain audio insert: tap the signal mid-chain, send to a
  hardware output, return from a hardware input, inline.
- **External Instrument** — drive a hardware synth: send MIDI to a hardware MIDI
  output, return the synth's audio from a hardware input, **on the same track**.

## Engine mechanism: TE `InsertPlugin`

Tracktion Engine already provides the whole capability — we integrate, we do not
build DSP/routing. `InsertPlugin`
(`plugins/internal/tracktion_InsertPlugin.h`) is a normal `te::Plugin` with:

- `CachedValue<String> outputDevice` — the **send** target (a hardware audio or
  MIDI output).
- `CachedValue<String> inputDevice` — the **return** source (a hardware audio
  input).
- `CachedValue<double> manualAdjustMs` — manual latency trim.
- `DeviceType {noDevice, audioDevice, midiDevice}` for send/return, refreshed by
  `updateDeviceTypes()`.
- `getPossibleDeviceNames(engine, …, forInput)` — enumerates eligible devices.
- `getLatencySeconds()` — feeds TE's automatic plugin-delay-compensation.

One class covers both features by send/return type:

- **External FX** = audio send + audio return (`isInstrument = false`, Effect slot).
- **External Instrument** = MIDI send + audio return (`isInstrument = true`,
  Instrument slot).

### Critical: the return is a graph-level send/return, NOT the record path

`InsertPlugin::applyToBuffer` is a dead stub:

```cpp
void InsertPlugin::applyToBuffer (const PluginRenderContext&) {
    jassertfalse; // This shouldn't be called anymore, it's handled directly by the playback graph
}
```

The send/return is wired directly into TE's playback graph at the insert's
position. It does **not** go through the track's input device, record-arm, or
input-monitoring. Consequences that drive the whole design:

- The synth/FX return is always active during playback with **no record-arm and
  no monitor toggle**.
- It does **not** consume the track's recording input — the track's audio-in
  selector stays exactly what it is today (recording only), untouched.
- Latency is auto-compensated in-chain (TE PDC sums `getLatencySeconds()`), plus
  the manual `manualAdjustMs` trim.

This is precisely why External Instrument must be a device and not track routing
(see "Rejected alternatives").

## Why both are devices (rejected alternatives)

We considered doing External Instrument purely with the track's existing
MIDI-out + audio-in (no device). Rejected because:

1. The synth's returned audio has nowhere to land on a MIDI track without either
   a **second (audio) track** or abusing live input-monitoring of the track's
   audio-in — which needs record-arm/monitor, doesn't render/bounce cleanly, and
   isn't latency-compensated. The track audio-in is meant for **recording**.
2. The `InsertPlugin` injects the return audio into the **same track's chain** at
   the insert position, so MIDI clips + resulting audio live on one track, render
   and freeze correctly, and get in-chain PDC.

So both features are `InsertPlugin` devices in the track chain.

## The "two MIDI-outs" question — suppression, not mirroring

A track already has a MIDI-out routing selector (track I/O). An External
Instrument device also routes MIDI out. These are **not** the same endpoint and
must **not** be mirrored:

- Track MIDI-out = the track's outgoing MIDI bus.
- Insert MIDI-out (send) = the synth destination, owned by the device.

Resolution: when an External Instrument device is present on a track, the device
owns MIDI routing, so **hide/disable the track-level MIDI-out** for that track.
The user only ever sees one MIDI-out (the device's). Same convention Ableton's
External Instrument uses. The track's audio-in and MIDI-in are unaffected
(recording / playing remain their own thing).

There is otherwise nothing to "keep in sync": the insert return never touches the
record path, so the only overlapping control (MIDI-out) is removed by suppression.

## Model & instantiation (small — rides the existing internal-plugin path)

`InsertPlugin` is a TE internal plugin, so it goes through the existing
`PluginFormat::Internal` path exactly like `TeCompressor`. **No** new
`PluginFormat`, **no** new `DeviceInfo` fields, **no** change to the VST
(`ExternalPlugin`) loader.

- `InternalDeviceKind` (`core/InternalDeviceKind.hpp`): add **one** kind,
  `ExternalInsert`, added to `shouldUseTracktionStringFactory`. (Not two: both
  FX and Instrument are the same `te::InsertPlugin` with `xmlTypeName "insert"`,
  and `classifyInternalDevice`/`findInternalPluginSpec` key on that id — two kinds
  would collide. The FX-vs-Instrument split is a per-`DeviceInfo` presentation
  concern, surfaced as two browser entries + default send/return config later.)
- `InternalPluginRegistry.cpp` `kSpecs[]`: one entry, `ExternalInsert`,
  `pluginId = te::InsertPlugin::xmlTypeName`, `createMode = SavedStateOrFresh`,
  `matches<te::InsertPlugin>`, `createProcessor = nullptr` (no param grid),
  `canCreateDetached = false` (no racks yet), category `"External"`.
  `showInBrowser` stays `false` until the picker UI lands (Phase 2).
  Status: **done (Phase 1)** — `classifyInternalDevice("insert") == ExternalInsert`,
  the spec resolves, app + tests build, covered by `test_external_insert_registry.cpp`.
- Persistence is **free**: `SavedStateOrFresh` round-trips the plugin's ValueTree
  as XML, and `name`/`inputDevice`/`outputDevice`/`manualAdjustMs` are
  `CachedValue` properties in that tree (see `restorePluginStateFromValueTree`).
- Instantiation is **free**: `createInternalPluginFromSpec` already creates any TE
  plugin by `xmlTypeName` via the plugin cache; `loadDeviceAsPlugin`'s Internal
  branch inserts it into `track->pluginList`.

Open question: the registry entry references a `DeviceProcessor` factory. An
`InsertPlugin` has no automatable-parameter grid worth showing — confirm whether
the internal path tolerates a no-/thin-processor or needs a minimal
`HardwareInsertProcessor`. This is the one model-side unknown.

## UI — port from track I/O (no new enumeration code)

The send/return pickers reuse the existing routing UI:

- `components/mixer/RoutingSelector` — generic dropdown
  (AudioIn/AudioOut/MidiIn/MidiOut), `{id,name}` options + `onSelectionChanged`.
- `components/mixer/RoutingSyncHelper` — `populateAudio{Input,Output}Options` /
  `populateMidi{Input,Output}Options` build the lists from the device manager /
  `MidiBridge`.

The device-slot body (host-slot contract already allows custom device UI) hosts:

- External FX: two `RoutingSelector`s — Send (AudioOut), Return (AudioIn).
- External Instrument: Send (MidiOut), Return (AudioIn).
- A manual-latency field bound to `manualAdjustMs`.

On change: set the `CachedValue`s + call `updateDeviceTypes()`, then capture state.

## Hardware I/O enablement (the real plumbing)

The chosen send/return audio & MIDI devices must be **enabled** in TE's
`DeviceManager`. Enablement must be **reference-counted across all consumers**
(record-armed/monitored track inputs + every insert send/return): a port is
enabled iff some consumer needs it; removing one insert must not disable a port
another consumer still uses. Today enablement is imperative
(`handlePlaybackContextReallocation` force-enables wave devices) — this becomes a
derive-from-all-consumers pass. Both the device pickers and the track I/O UI read
the same device lists (`RoutingSyncHelper`) and observe `DeviceManager` changes so
they stay live-consistent.

Graceful "device missing" state when a saved device name is absent on load.

## Freeze-to-audio capture (issue #1623 main item)

Offline export cannot capture outboard gear: TE's `Renderer` runs a private
offline node player, so the insert send transmits nothing and the return node is
absent (`createInsertReturnNode` resolves the live input device, which does not
exist offline). `realTimeRender` only paces the offline render to wall-clock; it
never routes through hardware. The fix is a real-time capture pass that turns
the insert's return into a normal audio clip, after which offline export just
works.

### Capture mechanism: hidden tap plugin after the InsertNode

The graph wires an enabled insert as `InsertNode(chain-so-far, insert,
returnNode)` where the post-InsertNode signal IS the return audio, PDC-aligned
into the timeline (`manualAdjustMs` included). A disabled insert is skipped
entirely (passthrough). So the capture point is a hidden
**`InsertCapturePlugin`** placed directly after the insert in
`teTrack->pluginList` — same pattern as `FollowerSourceTapPlugin`
(`PluginManager::ensureFollowerSourceTap`): created via the plugin cache from a
`ValueTree` with a custom `xmlTypeName`, registered in
`MagdaEngineBehaviour::createCustomPlugin`, never in the browser or the MAGDA
device model, removed with `deleteFromParent()` when done.

The plugin is a transparent passthrough. When armed with a capture window
`[start, end)` (edit seconds) it writes blocks that intersect the window to a
`juce::AudioFormatWriter::ThreadedWriter` (lock-free FIFO + background write
thread — the standard JUCE recording pattern), zero-padding to align the exact
start sample, and flags completion through an atomic once the window has fully
passed. Writer lifecycle (create/finalise) stays on the message thread.

### Orchestration (message thread)

`freeze(trackId, deviceId)`:

1. Compute the capture range: first clip start → last clip end on the track,
   plus a release tail (constant, ~2 s). Beats in the model; convert to seconds
   at the TE boundary.
2. Create the capture wav under the project media dir (`Freeze/`), insert the
   capture plugin after the insert, arm it with the range.
3. Disable transport looping for the pass, locate slightly before range start,
   `transport.play()`. A small progress window (timer-driven progress + Cancel —
   the transport plays in real time, the user hears the pass) tracks the
   plugin's completion atomic.
4. On completion or cancel: stop transport, restore position/loop, remove the
   plugin, finalise the writer (cancel deletes the partial file).
5. On success:
   - `ClipManager::createAudioClipBeats` at range start with the capture file +
     `syncClipToEngine` (same landing path as `recordingFinished`).
   - Mute the track's content clips (store their ids for unfreeze).
   - Bypass the insert **and every device before it** in the chain (store prior
     bypass states). The captured signal is post-insert (wet): with the insert
     disabled the frozen clip passes through the chain, and pre-insert devices
     must not re-process it. Post-insert devices stay live — only the
     hardware-dependent part of the chain is frozen, everything after remains
     editable and renders offline as normal.
   - Persist frozen state on the device (frozen flag, capture file, frozen clip
     id, muted clip ids, saved bypass states).

`unfreeze(trackId, deviceId)`: delete the frozen clip, unmute the stored clips,
restore bypass states, clear the state.

### UI

`ExternalInsertUI` gets a Freeze/Unfreeze button (disabled until both send and
return are configured) and a frozen badge. The export dialog warns when any
enabled, un-frozen external insert exists — the render would be silent for that
track (shares the guardrail infra below).

### Alignment note

The capture stamps file position from the block's edit time at the tap node, so
the captured clip aligns exactly as the live graph aligns the return
(auto-PDC + `manualAdjustMs`). Any residual constant offset heard on real
hardware is what `manualAdjustMs` is for. Needs verification with a physical
loopback.

## Phasing

1. **Model + registry** — DONE: `InternalDeviceKind` values, two `kSpecs[]`
   entries, string-factory wiring, `test_external_insert_registry.cpp`.
2. **Device-slot UI** — DONE: send/return pickers + manual-latency field wired
   to CachedValues + `updateDeviceTypes()`.
3. **Hardware I/O** — DONE (insert-scoped): `ExternalInsertDeviceEnablement`
   derives port enablement from the inserts (auto-enable on use, auto-disable
   only what it enabled once unused; user enables untouched); the pickers list
   disabled ports. Track-I/O consumers can be folded into the same derive pass
   later.
4. **MIDI-out suppression** — DONE as a read-only mirror: the track-level
   MIDI-out / audio-in display the device's selection and are only editable on
   the device.
5. **Tests & guardrails** — DONE: registry + freeze round-trip + capture-window
   tests; slot status line warns on shared send/return ports; MIDI sendback
   feedback guard in `MidiInputRouter`; still disallowed in racks.
6. **Freeze-to-audio capture** — DONE (section above): `InsertCapturePlugin` +
   `InsertFreezeService` + slot Freeze button + export warning. Hardware
   alignment verification outstanding.

## What's free vs. real effort

- Free / small: engine mechanism, instantiation, persistence, latency comp,
  model/registry, picker widget reuse.
- Real effort: device-slot UI wiring, and especially **reference-counted hardware
  I/O enablement** shared with track I/O.
