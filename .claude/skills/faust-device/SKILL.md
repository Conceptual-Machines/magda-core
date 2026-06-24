---
name: faust-device
description: Work on MAGDA's compiled Faust devices (synths/FX whose DSP is a .dsp file compiled to C++ at build time). Use when adding or changing a parameter on a compiled Faust plugin (e.g. Poly Synth, the filter engines, compiled FX), creating a new compiled Faust device, or wiring its custom UI. Covers the .dsp -> generated.cpp pipeline, the host-slot contract, and UI/linking.
---

# Compiled Faust devices

MAGDA has two kinds of Faust device:
- **Compiled** (this skill): the `.dsp` is compiled to C++ **at build time** and statically linked. Fast, ships in the binary. Lives in `magda/daw/audio/faust_dsp/*.dsp` + `magda/daw/audio/plugins/compiled/Magda*CompiledPlugin.{hpp,cpp}`.
- Interpreted/JIT (FaustPlugin / FaustInstrumentPlugin): loads `.dsp` at runtime. Not this skill.

## The pipeline (how a .dsp becomes a device)

```
faust_dsp/magda_x.dsp
  --(CMake: magda_compile_faust_dsp, runs `faust -lang cpp`)-->
cmake-build-debug/compiled_dsps/magda_x.generated.cpp   (NOT checked in)
  --(#include'd by)-->
plugins/compiled/MagdaXCompiledPlugin.cpp   (the host wrapper)
  --(custom UI)-->
ui/components/chain/custom_ui/XUI.cpp
```

- The generated `.cpp` is produced by the build — there is **no checked-in generated file** to hand-edit. Just edit the `.dsp` and rebuild (`make debug`); CMake reruns faust. Confirm the result in `cmake-build-debug/compiled_dsps/<name>.generated.cpp`.
- Registration lives in `magda/daw/audio/CMakeLists.txt` via `magda_compile_faust_dsp(<dsp> <ClassName> <OUT_VAR>)`; the generated path is added to `target_sources` and the wrapper `#include`s `<name>.generated.cpp`.
- Validate DSP before building with the `faust-mcp-magda` MCP tools (`compile_faust`, `search_faust_libraries`, `get_faust_library`).

## Host-slot contract (THE thing to get right)

Each control is a Faust `hslider`/`nentry` tagged `[idx:N]`. `N` is the **host slot index** — the stable contract between dsp, wrapper, and UI. The wrapper harvests zones by reading the `[idx:N]` metadata.

Three places must agree on slot indices:
1. **`.dsp`** — `[idx:N]` on each param.
2. **`MagdaXCompiledPlugin.hpp`** — `kFooSlot = N` constants + `kHostSlotCount`.
3. **`MagdaXCompiledPlugin.cpp`** — `hostSlotInfo_[kFooSlot] = {.name, .scale, .minValue, .maxValue, .defaultValue, .choices}`.
4. **`XUI.{hpp,cpp}`** — its own `kFooSlot` constants + `kNumParams`, label, layout.

### Adding a parameter (common case)

**Append at the end** (`idx = old kHostSlotCount`) so existing slot indices stay stable — never insert in the middle and renumber.

1. `.dsp`: add the `hslider`/`nentry` with the next free `[idx:N]`; wire it into the DSP graph. Reuse-by-copy from a sibling `.dsp` when matching behaviour (e.g. the drive lerp `(1-d)*x + d*(tanh(4x)/tanh(4))` from `magda_filter_svf.dsp`).
2. Wrapper `.hpp`: add `kFooSlot = N`, bump `kHostSlotCount`. The `std::array<…, kHostSlotCount>` members grow automatically.
3. Wrapper `.cpp`: add `hostSlotInfo_[kFooSlot] = {…}`. Use `ParameterScale::Discrete` + `.choices = {...}` for menus, `Linear`/`Logarithmic`/`FaderDB` otherwise.
4. UI: add `kFooSlot`, bump `kNumParams`, add a label, place it in `resized()`.
5. `make debug` (regenerates the dsp), confirm the param appears in the generated `.cpp`.

`[style:menu{'A':0;'B':1}]` in the dsp = a discrete choice param; mirror with `.choices` in slot info.

## Custom UI + linking

- The UI exposes `std::vector<LinkableTextSlider*> getLinkableSliders()`; each slider carries its slot via `setParamIndex()`. `DeviceSlotComponent::setupCustomUILinking()` wires mod/macro/automation/MIDI-Learn off that list. Any control that must be linkable has to be a `LinkableTextSlider` in that list.
- To present a slot as something else (e.g. segmented buttons for a menu): keep the hidden `LinkableTextSlider` in `controls_`/`getLinkableSliders()` (carries value + linking) and drive it from the custom widget. Set selection **explicitly** for segmented buttons (`setToggleState(i==sel)`), not via JUCE radio groups (radio exclusivity left two segments lit).
- Theme-font buttons: `setLookAndFeel(&FlatTabButtonLookAndFeel::getInstance())` (flat tab) or `SmallButtonLookAndFeel` (rounded), both in `themes/SmallButtonLookAndFeel.hpp`; clear with `setLookAndFeel(nullptr)` in the destructor.
- Labels/fonts: `FontManager::getInstance().getUIFont(...)`, colours via `DarkTheme::getColour(...)`.

## Reusing a curve view across devices

Curve/response views (e.g. `CompiledFilterCurveView`) are normally bound to one plugin's slots via a `DeviceInfo` snapshot. To reuse one in a different device, add a **raw-value entry point** (`setRawState(...)`) that sets the target values directly and bypasses the device-snapshot path, plus optional per-instance theming (e.g. `setCurveColour`). Drive it from the host UI on every relevant param change.

## Gotchas

- Generated `.cpp` is build output — don't look for it in git; rebuild to refresh.
- Never reorder/insert `[idx:N]`; append. (Unreleased features: no migration needed, but stable indices keep diffs sane.)
- `ba.selectn(n, sel, ...)` is strict — every branch runs, so cascading/parallel filter modes is cheap and safe.
- Polyphonic synths use Faust's `mydsp_poly` (group=false) — per-voice zones are harvested into `voiceZonesBySlot_`.
- Build with the `building` skill (`make debug`), never raw faust/cmake.
