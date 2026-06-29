# Issue 1631: MAGDA Custom UI Plugin Coupling Inventory

Parent epic: #1630

This inventory covers MAGDA device UI code under `magda/daw/ui/components/chain`.
It classifies long-lived live-plugin references by why the UI currently needs the
plugin and what interface should replace the concrete pointer.

## Summary

Most custom UIs are already parameter-model driven: they receive
`ParameterInfo`, expose `LinkableTextSlider` controls, and write parameter
changes through callbacks wired by `DeviceCustomUIManager`. The remaining
coupling falls into four patterns:

- telemetry/snapshot polling from a live audio plugin;
- command surfaces that call plugin-specific methods;
- compiled-effect presentation views that use concrete helpers for display
  conversion, collapse state, tempo divisions, or meters;
- drum-grid slot plumbing that still keeps Tracktion plugin pointers as UI
  state.

The highest crash risk is telemetry or async UI code that keeps a raw pointer
and can outlive a removed/rebuilt plugin.

## Direct Custom UI Coupling

| UI | Owner file(s) | Plugin type | Usage category | Risk | Target interface |
| --- | --- | --- | --- | --- | --- |
| `OscilloscopeUI` | `custom_ui/OscilloscopeUI.hpp`, `custom_ui/OscilloscopeUI.cpp` | `daw::audio::OscilloscopePlugin*` | Telemetry/snapshot and popout forwarding | High: timer/popout can poll a removed plugin | `OscilloscopeTelemetryProvider` plus lifetime token from `DeviceUiContext` |
| `SpectrumAnalyzerUI` | `custom_ui/SpectrumAnalyzerUI.hpp`, `custom_ui/SpectrumAnalyzerUI.cpp` | `daw::audio::SpectrumAnalyzerPlugin*` | Telemetry/snapshot, masking overlay, popout forwarding | High: timer/popout can poll a removed plugin | `SpectrumTelemetryProvider` plus lifetime token from `DeviceUiContext` |
| `LevelsUI` | `custom_ui/LevelsUI.hpp`, `custom_ui/LevelsUI.cpp` | `daw::audio::LevelsPlugin*` | Telemetry/snapshot; also gates plugin measurement while visible | High: timer polls live plugin and toggles plugin activity | `LevelsTelemetryProvider` with explicit measurement subscription handle |
| `NimbusUI` | `custom_ui/NimbusUI.hpp`, `custom_ui/NimbusUI.cpp` | `daw::audio::MutableCloudsPlugin*` | Parameter-control UI plus live grain-buffer telemetry | Medium/high: timer reads live buffer data; controls already callback-driven | `NimbusTelemetryProvider`; keep writes through parameter callbacks |
| `FaustUI` | `custom_ui/FaustUI.hpp`, `custom_ui/FaustUI.cpp` | `daw::audio::IFaustEditorModel*` | Special command surface for DSP load/save/edit and dynamic parameter refresh | High: async popup/file chooser callbacks capture `this` and use `plugin_` | `FaustEditorController` plus weak/lifetime token and host notification callback |
| `FaustInstrumentTabbedUI` | `custom_ui/FaustInstrumentTabbedUI.hpp`, `custom_ui/FaustInstrumentTabbedUI.cpp` | `daw::audio::IFaustEditorModel*` | Container/header forwarding to `FaustUI` | Same as `FaustUI`, but indirect | Depend on the same `FaustEditorController` |
| `StepSequencerUI` | `custom_ui/StepSequencerUI.hpp`, `custom_ui/StepSequencerUI.cpp` | `daw::audio::StepSequencerPlugin*` | Command surface and pattern drag/export state | Medium: live command calls, but less timer-driven telemetry | `StepSequencerController` for pattern mutation/export plus parameter callbacks |
| `PolyStepSequencerUI` | `custom_ui/PolyStepSequencerUI.hpp`, `custom_ui/PolyStepSequencerUI.cpp` | `daw::audio::PolyStepSequencerPlugin*`; downstream `DrumGridPlugin*` lookup | Command surface, pattern drag/export, downstream drum-grid discovery | Medium: multiple nested subviews keep plugin pointers | `PolyStepSequencerController`; move downstream routing lookup behind core service |
| `StrumUI` | `custom_ui/StrumUI.hpp`, `custom_ui/StrumUI.cpp` | `daw::audio::MidiStrumPlugin*` | Command/state surface for MIDI strum UI | Medium: command calls against live plugin | `MidiStrumController`; parameter writes stay callback-based |
| `ArpeggiatorUI` | `custom_ui/ArpeggiatorUI.hpp`, `custom_ui/ArpeggiatorUI.cpp` | `daw::audio::ArpeggiatorPlugin*` | Command/state surface for arp UI | Medium: command calls against live plugin | `ArpeggiatorController`; parameter writes stay callback-based |
| `StruckInstrumentUI` | `custom_ui/StruckInstrumentUI.hpp`, `custom_ui/StruckInstrumentUI.cpp` | `compiled::MagdaCompiledPolyInstrument*` | Parameter-control UI plus strike-pulse telemetry | Medium: timer polls note-on pulse from live plugin | `PolyInstrumentTelemetryProvider` |

## Compiled Effect Presentation Views

The compiled views under `magda/daw/ui/components/chain/compiled` share the same
pattern: `CompiledPluginPresentation::bindPlugin(te::Plugin*)` downcasts to a
concrete `Magda*CompiledPlugin*`, stores `compiledPlugin_`, and uses plugin-only
helpers for display conversion, snapshots, meters, tempo divisions, or UI state.

Affected views found in this pass:

- `CompiledBitcrusherEditorView`
- `CompiledChorusCurveView`
- `CompiledClipperCurveView`
- `CompiledCompressorCurveView`
- `CompiledDelayCurveView`
- `CompiledDimensionView`
- `CompiledEqCurveView`
- `CompiledFilterCurveView`
- `CompiledFlangerCurveView`
- `CompiledFreqShiftCurveView`
- `CompiledGateCurveView`
- `CompiledGrainDelayCurveView`
- `CompiledGritCurveView`
- `CompiledLimiterCurveView`
- `CompiledModCurveView`
- `CompiledMultibandCurveView`
- `CompiledPhaserCurveView`
- `CompiledPitchEditorView`
- `CompiledReverbCurveView`
- `CompiledRingModCurveView`
- `CompiledSaturatorCurveView`
- `CompiledUtilityView`

Migration target: a shared compiled-device presentation interface that exposes
slot display conversion, snapshot/telemetry reads, tempo-division helpers, and
small persisted UI state such as curve collapse. Parameter writes should remain
host/controller mediated.

Risk level: medium overall, high for views that poll meters or analyzer buffers
on timers (`CompiledEqCurveView`, `CompiledCompressorCurveView`,
`CompiledLimiterCurveView`, and related dynamics/visualizer views).

## Manager and Binding Hotspots

| Owner file | Coupling | Notes | Target |
| --- | --- | --- | --- |
| `slot/DeviceCustomUIManager.hpp` | Stores raw pointers for arp, strum, step sequencer, poly step sequencer, and chord plugin | Transitional state used by timers, path rebinding, and external control helpers | Replace with controller handles stored in `DeviceUiContext` |
| `slot/DeviceCustomUIManager.cpp` | Downcasts live Tracktion plugins and calls `setPlugin(...)`/`setArpeggiator(...)`/`setLivePlugin(...)` | Central place to insert controller/telemetry adapters | Adapter factory keyed by `DeviceInfo`/`ChainNodePath` |
| `slot/DeviceSlotMidiUiBinding.cpp` | Rebinds MIDI custom UIs to live plugin types | Duplicates some manager binding logic | Route through the same MIDI controller adapters |
| `slot/DeviceSlotAnalyzerContextActions.cpp` | Creates analyzer popout UIs and binds live analyzer plugins | Popouts are especially sensitive to plugin lifetime | Popout receives telemetry provider and lifetime token |
| `slot/DeviceSlotInlineUiFactory.cpp` | Binds inline Faust UI to `FaustPlugin`/`IFaustEditorModel` | Same command-surface issue as `FaustUI` | `FaustEditorController` |
| `drum_grid/DrumGridUI.hpp`, `drum_grid/DrumGridUI.cpp` | Stores `DrumGridPlugin*` | Drum-grid command/state surface | `DrumGridController` |
| `drum_grid/PadDeviceSlot.hpp`, `drum_grid/PadDeviceSlot.cpp` | Stores `tracktion::engine::Plugin*` and accepts sampler/external plugins | Nested rack/pad UI keeps live plugin state | Pad-scoped `DeviceUiContext` and sampler controller |
| `drum_grid/PadChainPanel.hpp`, `drum_grid/PadChainPanel.cpp` | Stores collapsed `tracktion::engine::Plugin*` vectors | UI state is keyed by live plugin pointers | Stable device/path ids instead of plugin pointers |

## Already Mostly Decoupled

These UIs are primarily parameter-model driven and do not store a concrete live
plugin pointer in their own class:

- `SamplerUI`
- `FourOscUI`
- `PolySynthUI`
- `FMUI`
- `MateriaUI`
- `HaloUI`
- `DrumVoiceUI`
- simple effect UIs such as `EqualiserUI`, `CompressorUI`, `ReverbUI`,
  `DelayUI`, `ChorusUI`, `PhaserUI`, `FilterUI`, `PitchShiftUI`, and
  `ImpulseResponseUI`

Some of these still receive live-plugin-derived data through
`DeviceCustomUIManager` during creation or update. They should stay on the
existing callback/`ParameterInfo` path unless a specific telemetry or command
need appears.

## Proposed Migration Order

1. Define `DeviceUiContext` with parameter/controller access, telemetry provider
   lookup, and a lifetime token that can invalidate UI callbacks after rebuild or
   removal.
2. Migrate the high-risk telemetry UIs first: oscilloscope, spectrum analyzer,
   levels, and Nimbus.
3. Migrate `FaustUI` to a command controller with weak async callbacks before
   adding more premium-device command surfaces.
4. Migrate sequencer/MIDI command UIs to controller interfaces.
5. Consolidate compiled-effect views behind a shared presentation/telemetry
   interface.
6. Replace drum-grid pad live-plugin pointers with stable path/device ids and
   pad-scoped UI contexts.
