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
validated this way in full. Existing TE-native compatibility devices are
compiled in a host-owned compatibility target, outside the SDK pack, until
their DSP implementations are migrated to `MagdaDevice`.

The transitional all-compiled-device catalog is also host-owned while it
references both neutral and TE-native specs. The `magda_base_devices` archive
contains only neutral device implementations and therefore has no unresolved
references to compatibility-device spec accessors.

## Optional private pack

Configure with:

```sh
cmake -S . -B build \
  -DMAGDA_PRO_DEVICES=ON \
  -DMAGDA_PRO_DEVICES_DIR=/path/to/private/device-pack
```

The directory must provide a CMake target named `magda_pro_devices`, link it
privately to `magda::device_sdk`, and register its devices through
`registerDevicePack()`. The in-tree `device_packs/pro_stub` directory is the
default proof pack and can be used to validate the integration without the
private repository.
