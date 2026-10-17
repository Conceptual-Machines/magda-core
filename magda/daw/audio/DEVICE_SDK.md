# MAGDA device SDK surface

Device packs are build-time static libraries. They compile against
`magda::device_sdk`, register an `InternalPluginSpec` through
`registerDevicePack()`, and are retained by the host with whole-archive
linking. This is a C++ source/build contract, not a stable dynamic ABI.

Concrete devices implement the engine-neutral `MagdaDevice` contract. The
active audio engine owns the adapter that presents that device to its host
lifecycle; a device pack does not subclass an engine plugin class.
`DeviceProcessContext` carries audio, MIDI, transport, and a read-only tempo
map. Device-owned telemetry is exposed through typed `DeviceTelemetry`
subclasses, while host/editor lifetime remains outside the device.

MIDI is split by direction. `midiIn` is read-only. `midiOut` is empty on entry
and, on exit, is the device's whole MIDI output; a device only has one if it
declares `producesMidi` in `DeviceProperties`. The chain's raw MIDI never
passes through a device: thru is the host's merge behind it
(`DeviceInfo::midiInThru`, #2347).

A sidechain key is a declared port, not extra channels of the buffer. A device
says what it takes in `DeviceProperties::sidechain` -- `{None, Audio(channels),
MIDI}` -- and reads it from `DeviceProcessContext::sidechain`, which carries
`numSidechainChannels` read-only channels indexed from `startSample` like the
audio. Zero channels means nothing is routed, which is not the same as a silent
key. Declaring more inputs than outputs is not a request for one (#2329).

A compiled Faust device declares its key by overriding
`MagdaCompiledEffect::sidechainPort()`; `MagdaCompiledEffect` copies the key
into the dsp inputs after the device's own, so no dsp reads the port itself. A
runtime Faust patch declares it in the source, with `declare magda_sidechain
"audio";`, and the key is the inputs the process function takes past its
outputs.

## Public surface

A pack may use:

- `magda_types` value headers under `core/`
- `magda_music` theory helpers
- `plugins/MagdaDevice.hpp` for identity, lifecycle, audio/MIDI processing,
  parameters, and state
- `plugins/DeviceServices.hpp` for injected DAW services and defaults
- `plugins/InternalPluginRegistry.hpp` for pack registration
- opaque `DevicePluginHandle`, `DeviceParameterHandle`, and `DeviceSessionKey`
  values at transitional host boundaries
- `plugins/IFaustEditorModel.hpp`
- `magda_compile_faust_dsp()` plus the Faust and Mutable toolchains supplied by
  the host build

Pack sources must not include host-owned `core/` implementation headers,
`engine/`, `project/`, `ui/`, `plugin_manager/`, `racks/`, or `modifiers/`.
`magda_validate_device_pack_sources()` enforces that boundary at configure
time.

`InternalPluginSpec::createDevice` is the normal factory hook. The
`createPlugin` and `createInSession` opaque-handle hooks exist only while
legacy host-native devices are migrated; new packs should not use them.
`matchesPlugin` and `createProcessor` are likewise host-owned compatibility
metadata for MAGDA's built-in legacy catalog. Engine-neutral packs leave all
four compatibility callbacks null. Processor dispatch itself is implemented
by the active host adapter and is not part of the device SDK.
Packs pass `ENGINE_NEUTRAL` to `magda_validate_device_pack_sources()` so
configure fails if a source includes or names Tracktion. MAGDA's base pack is
validated this way in full. The `magda_base_devices` archive contains only
neutral device implementations.

## Packs built outside this repository

A pack outside the tree is built by an app extension (`magda/daw/extensions/Extension.hpp`).
The extension's directory is named at configure time:

```sh
cmake -S . -B build -DMAGDA_EXTENSION_DIRS=/path/to/extension
```

Its CMakeLists defines the pack target, linked privately to `magda::device_sdk` and validated
with `magda_validate_device_pack_sources()`, and hands it to the app with
`magda_add_extension(<target>)`. The pack's frozen parameter order goes in its own file,
named with `magda_add_extension_param_schema()`. The in-tree `extensions/stub` directory is
the proof extension CI builds.
