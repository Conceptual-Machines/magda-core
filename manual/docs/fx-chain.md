# FX Chain & Racks

Each track has an **FX chain** — an ordered list of audio processors applied to the track's signal.

![Track Chain](assets/images/panels/track-chain.png)

## Overview

The FX chain is displayed in the bottom panel when a track or device is selected. Signal flows left to right through the chain.

!!! note
    The chain header provides access to the **track tree** ![tree](assets/images/icons/tree.svg){ width="16" }, a hierarchical view of all devices and racks on the track. You can also click the **rack** button ![rack](assets/images/icons/rack.svg){ width="16" } to create a new rack in the chain.

The chain can contain:

- **Plugins** — VST3, AU, or VST effect and instrument plugins
- **Built-in devices** — MAGDA's own processors (see [Built-in Devices](devices/built-in.md))
- **Racks** — Container devices with nested chains and parallel routing

## Working with Devices

- **Drag** plugins from the [Plugin Browser](panels/browsers.md) onto the chain to add them
- **Drag** devices to reorder them in the chain
- **Click** a device to select it and show its parameters
- **Right-click** a device for options: bypass, remove, replace, move to rack
- Each device has **Mod** and **Macro** buttons to toggle the modulation and macro panels
- Plugin devices have a **Learn** button that maps a control in the plugin's own UI back to the matching parameter slot — see [Plugin Parameters](plugin-parameters.md#learn-mode)

See [Plugin Parameters](plugin-parameters.md) for per-plugin parameter visibility, custom units and ranges, and AI-assisted classification.

## Presets

Three preset systems share the device header:

### MAGDA Presets (.mps)

Each device exposes a preset menu in its header. Saving a preset captures **everything MAGDA knows about the device** — parameter values, plugin state, macros, modulators, sidechain wiring, gain — in a single `.mps` file.

- **Save Preset** — pops a name + category dialog. The selected category becomes a folder under the preset directory.
- **Load Preset** — submenu organised by category. Loading replaces the live device's state in place; the device's id, name, and slot in the chain are preserved.
- The dialog auto-fills the most recently used name and category, so re-saving an iteration takes two clicks.

`.mps` is portable across MAGDA installations. The format is plugin-aware — loading a preset onto a slot whose plugin id doesn't match is rejected with a one-line error rather than silently overwriting state.

### Plugin Disk Presets

When the device is a third-party plugin (VST3 / AU), MAGDA also scans the standard `.vstpreset` / `.aupreset` directories on disk and exposes them as a second menu in the header. The native preset format is used directly — your existing factory and user banks are picked up without a copy step.

The legacy "Program N" menu that VST3 plugins like Vital and Serum 2 expose has been retired — those entries resolve to identical state and provided no information. Disk presets are the only source.

### Track Chain Presets

The track FX header carries a chain-level preset menu. A track-chain preset captures the **entire chain** — every device, every nested rack, every modulator binding — for a single track. Useful for stashing a finished mix-bus chain, a vocal chain, or a synth-and-FX combo and dropping it onto a fresh track later.

## Racks

![Rack](assets/images/devices/rack.png)

A rack is a container that holds one or more **parallel chains**. Signal flows into the rack, splits across chains, and mixes back together at the output.

### Creating a Rack

- Click the ![rack](assets/images/icons/rack.svg){ width="16" } **rack** button in the chain header
- Right-click a device and select **Move to Rack** to wrap it in a new rack

### Chains

Each rack contains one or more chains displayed as rows. Each chain has:

- **Volume** — Level control for the chain's output
- **Pan** — Stereo positioning of the chain
- **Mute** (M) / **Solo** (S) — Mute or solo individual chains
- **Devices** — Each chain has its own ordered list of devices

Click the **+** button next to "Chains" to add a new parallel chain.

### Use Cases

- **Parallel processing** — Blend dry and wet signals (e.g., parallel compression)
- **Multi-band processing** — Split signal into frequency bands with different processing
- **Organized grouping** — Keep related devices together in a collapsible container

### Rack Controls

- **Volume** — Master output level for the entire rack
- **Mod / Macro buttons** — Access the rack's own modulation and macro panels
- **Collapse/Expand** — Click the rack header to toggle between collapsed and expanded views

### Collapsible Devices

Individual devices inside a rack can be collapsed to save space. Click the collapse button on a device to shrink it to a compact header. The rack resizes dynamically as devices are collapsed or expanded. Collapsed states are preserved when the chain rebuilds (e.g. when adding or removing devices).
