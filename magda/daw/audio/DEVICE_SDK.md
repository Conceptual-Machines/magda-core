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
- `plugins/IFaustEditorModel.hpp` and
  `plugins/FaustCustomViewKind.hpp`
- `magda_compile_faust_dsp()` plus the Faust and Mutable toolchains supplied by
  the host build

Pack sources must not include host-owned `core/` implementation headers,
`engine/`, `project/`, `ui/`, `plugin_manager/`, `racks/`, or `modifiers/`.
`magda_validate_device_pack_sources()` enforces that boundary at configure
time.

`InternalPluginSpec::createDevice` is the normal factory hook. The
`createPlugin` and `createInSession` opaque-handle hooks exist only while
legacy host-native devices are migrated; new packs should not use them.
Packs pass `ENGINE_NEUTRAL` to `magda_validate_device_pack_sources()` so
configure fails if a source includes or names Tracktion. MAGDA's base pack is
validated this way in full. Existing TE-native compatibility devices are
compiled in a host-owned compatibility target, outside the SDK pack, until
their DSP implementations are migrated to `MagdaDevice`.

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
