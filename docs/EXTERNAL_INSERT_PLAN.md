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

## Phasing

1. **Model + registry**: `InternalDeviceKind` values, two `kSpecs[]` entries,
   string-factory wiring. Resolve the processor open-question. Verifiable by
   build + a state round-trip test (mirror existing plugin-state tests).
2. **Device-slot UI**: port `RoutingSelector` + `RoutingSyncHelper`; send/return
   pickers + manual-latency field; wire to CachedValues + `updateDeviceTypes()`.
3. **Hardware I/O**: reference-counted device enablement across tracks + inserts;
   shared device lists + `DeviceManager` change observation; missing-device state.
4. **MIDI-out suppression**: hide/disable track MIDI-out when an External
   Instrument device is on the track.
5. **Tests & guardrails**: round-trip test; warn on the same hardware port driven
   from two places; disallow in racks initially.

## What's free vs. real effort

- Free / small: engine mechanism, instantiation, persistence, latency comp,
  model/registry, picker widget reuse.
- Real effort: device-slot UI wiring, and especially **reference-counted hardware
  I/O enablement** shared with track I/O.
