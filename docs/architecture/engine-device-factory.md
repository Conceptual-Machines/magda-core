# Engine device factory

`magda/daw/audio/plugins/engine/EngineDeviceFactory.hpp` turns a plan's
`Device` op (an identity, no plugin) into a runnable `EngineDevice`. A plan is
topology; binding an identity to a plugin instance is the host's job, the way
opening a WAV is (#2174).

## Internal devices (top half of the file)

Resolution asks the same two catalogs the current (TE) engine asks — the
internal registry and the compiled-Faust catalog — rather than keeping a
third list of what exists. It returns null for anything not yet ported to the
SDK (`InternalPluginSpec`/`CompiledPluginSpec` both carry `createDevice`).
That null must stay visible: a caller has to report an unrunnable device
rather than silently pass signal through, because a stand-in the incumbent
engine doesn't have is a divergence disguised as a null.

Parameters aren't written here — the plan's value layer resolves them per
block and the adapter writes them before each `process()`, so a device starts
at its defaults and is at the project's values by the first sample. The rest
of a device's state (`MagdaDevice::restoreState()`, the pluginState v2
contract, #1887) belongs to the state slice, not this one.

`canCreateEngineDevice` vs. `isRegisteredDevice`: the first asks "can the
engine build this", the second "does either catalog know this id at all".
`isRegisteredDevice == false` is normal (unset chain slot, external plugin,
test fixture) — the engine running nothing is what every host does there.
`isRegisteredDevice == true` with `canCreateEngineDevice == false` is the case
worth reporting: the app knows this device but the native engine can't build
it, so a render that passed audio through would be a render of a different
project.

## External plugins (bottom half of the file, #2243)

An external plugin is a file on a machine, not a class this build contains —
it can fail to load for reasons worth telling a user (moved, upgraded,
uninstalled), so these entry points return *why* rather than a null.

Unlike an internal device, an external plugin's saved state does travel:
MAGDA persists the plugin's own chunk, and the adapter applies it along with
the saved parameter array it overlays (`EngineExternalDevice::applySavedState`).
Writing the chunk back out of a native-engine instance so a project
round-trips between engines during the dual-engine release is the other half,
tracked separately (#2244).

### The synchronous path

`adaptExternalPluginInstance` is one transaction over an instance the host
created: enable all buses (must happen before channel counts can be read),
apply the saved parameter array, then the chunk (chunk wins on conflict), and
hand back what the plugin holds now in `ExternalDeviceResult::restoredParameters`.

The engine never corrects the model itself — the model is the one authority
and this is a render path — so the correction is handed back for the caller
to apply on the message thread (`magda::applyRestoredParameters`). A caller
whose model already matches the plugin (e.g. a project a newer build already
refreshed) just re-applies the same values.

`createEngineExternalDevice` blocks until the plugin loads, which suits a
render with nothing else to do meanwhile (offline bounce, corpus case). A
session opening a project with many plugins needs the async form instead.

### The asynchronous path

Loading is async because a plugin can take seconds and a project keeps
running while it does. By completion time the device may have been edited,
had a preset applied, or been deleted — so:

- `CurrentDeviceLookup` reads the model **at completion**, not captured at
  request time, so restoration doesn't clobber whatever happened while the
  plugin was loading. It's keyed on `engine::DeviceKey`, not a bare
  `DeviceId`, because `DeviceId` is allocated per section (main FX, post-FX,
  mixer-analysis), so a bare id can name up to three different devices.

- `RequestedPlugin` is a self-contained, weak-reference snapshot of the
  question being answered (`PluginAssignments.hpp`), deliberately excluding
  saved plugin metadata — resolving a moved plugin or an imported DAWproject
  may legitimately correct that before the request completes.

- `completeExternalPluginLoad` enforces an **identity boundary**: it only
  completes onto the assignment it was requested for. It refuses the load if
  that assignment was deleted, had its plugin replaced, was duplicated or
  pasted from, had its id reused after a project clear, was never
  registered, or if the whole runtime went away while loading. Because the
  check needs only the requested snapshot, it works even after the runtime
  that issued the request no longer exists.

- `createEngineExternalDeviceAsync` reads the saved device once (to decide
  what to load) and reads `currentDevice` again at completion (to decide what
  to restore) — two different questions, both above. `key`/`assignments` are
  read once, here, to build a self-contained request; the runtime is not
  captured, so it destructing mid-load makes the request expire rather than
  dangle. The same expiry gates the `completed` callback: if the runtime that
  owns `assignments` is destroyed first, `completed` is simply never called —
  a caller only needs to keep `assignments` alive for as long as it wants to
  hear about the load, nothing more. Until a device binds, the executor's
  existing pass-through behavior applies (op passes audio through, reports it
  in diagnostics); completion re-prepares the plan rather than hot-swapping
  the device in.

`isInstalledExternalPlugin` only says a scanned plugin exists to try loading
— it does not promise instantiation will succeed.
