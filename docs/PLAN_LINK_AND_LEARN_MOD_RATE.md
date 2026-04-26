# Plan: link macros / mods to LFO rate, MIDI Learn for macros & rate

Two parallel branches off `main` once `feat/automate-link-mod-rate` is merged.
Both should branch from the same commit. They do **not** share files at the data-model layer, so they can develop and merge independently.

If a conflict surfaces, it's almost certainly in `MacroKnobComponent.cpp` (both branches add a right-click menu item) or `ModulatorEditorPanel.cpp` (Branch B adds a "MIDI Learn" item to the rate-slider menu Branch A wires for "Add Mod"). Coordination notes at the end.

---

## Branch A — `feat/link-macro-mod-to-rate`

Lets a macro or another modifier drive an LFO's `Rate`. Same TE attachment mechanism we already use for device parameters; the only new thing is *which* `te::AutomatableParameter` we attach to.

### Data-model changes

- **`MacroTarget`** (`magda/daw/core/MacroInfo.hpp`) and **`ModTarget`** (`magda/daw/core/ModInfo.hpp`) currently address only device parameters (`deviceId + paramIndex`). Add a target-kind discriminator:

  ```cpp
  enum class Kind { DeviceParam, ModParam };
  Kind kind = Kind::DeviceParam;
  // For ModParam:
  ModId modId = INVALID_MOD_ID;
  int  modParamIndex = -1;       // 0 = Rate (only one we support today)
  ```

  Keep `deviceId + paramIndex` populated for `DeviceParam` (no change). For `ModParam`, `deviceId` carries the path's *owning* device/rack id (or stays invalid for track-level mods) so the resolver knows where to find the modifier.

- **Equality, validity, string keys**: update `operator==`, `isValid()`, and any hash / serializer to include the new fields. Tests in `tests/test_macro_info.cpp` and `tests/test_mod_info.cpp` should round-trip both kinds.

### Serialization

- `magda/daw/project/serialization/AutomationModSerializer.cpp` (and the macro equivalent if separate) — add `kind`, `modId`, `modParamIndex` keys with default-on-missing legacy fallback so old projects keep loading as `DeviceParam`.

### TE attachment — the actual wire

- `RackSyncManager.cpp` and `PluginManager.cpp` both have macro/mod attachment loops that do `param->addModifier(*macroParam, link.amount)` against a *plugin* parameter resolved from the link's `deviceId + paramIndex`. Add a branch:

  ```cpp
  if (link.target.kind == MacroTarget::Kind::ModParam) {
      // Resolve the LFO modifier for link.target.modId (rack/device/track scope
      // — same walk we use everywhere else; helper exists in
      //  PluginManager::findModifierParameterForAutomation).
      // Pick rate vs rateType using the modifier's tempoSync flag — exact
      // same branch the unified Rate lane uses; the te-host-parameter-writes
      // skill documents this.
      auto* teParam = findModifierTargetForLink(link.target);
      if (teParam) param->addModifier(*macroParam, link.amount);
  }
  ```

  Mirror the call inside `updateAllModifierProperties` so depth changes (drag of the link amount) propagate without a full re-attach.

- Detach path: when a link is removed, `param->removeModifier(*macroParam)` against the same `teParam`.

### UI

- **`MacroKnobComponent::showLinkMenu`** — under the existing `"Link to Parameter..."` section, add a **`Modulators`** submenu listing each LFO/Random/Envelope on the same scope (track, rack, or device — depends on macro's `parentPath_`). For each, an entry `Rate` (and later `Depth` when that lane lands).
- **`ModulatorEditorPanel`** + **`ModsPanelComponent`** — same pattern. The "Mod link target" picker (currently device params only) gets a `Modulators` submenu listing other mods on the scope. Don't link a mod to itself — filter the current mod out.
- The right-click "Show Automation Lane" item we just added stays where it is on both knobs/sliders.

### Out-of-scope for Branch A

- Modulator depth automation lane (separate future work)
- Cross-track linking (only same-track / same-rack scope for now — matches existing macro link behaviour)

### Touched files (Branch A)

```
magda/daw/core/MacroInfo.hpp
magda/daw/core/ModInfo.hpp
magda/daw/project/serialization/AutomationModSerializer.cpp
magda/daw/audio/RackSyncManager.cpp
magda/daw/audio/PluginManager.cpp
magda/daw/audio/PluginManager.hpp                        (if findModifierTargetForLink lives there)
magda/daw/ui/components/chain/MacroKnobComponent.cpp     (link menu submenu)
magda/daw/ui/components/chain/ModulatorEditorPanel.cpp   (link picker submenu)
magda/daw/ui/components/chain/ModsPanelComponent.cpp     (if it owns a separate link picker)
tests/test_macro_info.cpp                                (target round-trip)
tests/test_mod_info.cpp                                  (target round-trip)
```

---

## Branch B — `feat/midi-learn-macro-rate`

Bind a MIDI controller (CC, note, etc.) to a macro value or to an LFO rate. Independent of Branch A: writes go through existing `TrackManager::setXxxModRate` / `setMacroValue` setters — no new modulator-graph wiring.

### Data-model changes

- **`StaticTarget::Owner`** (`magda/daw/core/aliases/Target.hpp`) currently has `PluginParam`, `DeviceMacro`. Add:

  ```cpp
  enum class Owner {
      PluginParam,
      DeviceMacro,
      MacroValue,   // controller drives a macro's normalized value
      ModRate,      // controller drives an LFO's rate (Hz or sync ordinal — branches on tempoSync)
  };
  ```

  For `MacroValue`: `paramIndex` = macroIndex. `devicePath` = scope (rack/device/track-level).
  For `ModRate`: stash `modId` somewhere — either repurpose `paramIndex` as `modId` (cheap but read-confusing) or add a `modId` field. Adding the field is cleaner and matches Branch A's approach.

### Serialization

- `BindingRegistry` save/load needs to round-trip the new owners. `Binding.hpp` and the JSON serializer in `BindingRegistry.cpp` (or wherever) need entries for the new variants.

### Resolution + writeback

- `ControllerParamWriter.cpp` (or whatever component drains MIDI events into parameter writes) — extend the resolver:

  - `MacroValue` → `TrackManager::setXxxMacroValue(devicePath, macroIndex, normalized)` — pick rack/device/track flavour from `devicePath.getType()`, same dispatch we already use in `AutomationPlaybackEngine::writeModRateFromCurve`.
  - `ModRate` → `TrackManager::setXxxModRate(...)` for Hz mode, `setXxxModSyncDivision(...)` for sync mode. Same `tempoSync` branch the lane writeback uses; you can copy the resolution from `AutomationPlaybackEngine::writeModRateFromCurve` almost verbatim — pull it into a shared helper if you find yourself duplicating > ~30 lines.

- The MIDI thread's resolver MUST NOT lock or allocate. Cached snapshot of bindings already exists in `BindingRegistry`; piggyback on the existing snapshot path.

### UI

- **`MacroKnobComponent::showLinkMenu`** — add an entry above (or alongside) `Show Automation Lane`:

  ```
  MIDI Learn
  Remove MIDI Binding   (only when one exists)
  ─────────────
  Show Automation Lane
  ─────────────
  Link to Parameter... (existing)
  ```

  The "MIDI Learn" item arms `MidiLearnCoordinator::startLearn(target)` with `Owner::MacroValue` + the macro's path/index. Coordinator already handles the "next CC wins" UX.

- **`ModulatorEditorPanel::showRateSliderContextMenu`** — same two new items (`MIDI Learn`, `Remove MIDI Binding`) on the unified rate slider menu, with `Owner::ModRate` + the mod's path/id. Both the Hz slider and the sync-division slider feed this same menu (already do today).

- Optional: visual indicator (small dot / badge) on the knob/slider when a binding exists. The macro knob already has `hasAutomap_` for an automap-binding indicator — reuse that paint path with a different colour for explicit Learn bindings if it's quick; defer otherwise.

### Touched files (Branch B)

```
magda/daw/core/aliases/Target.hpp                     (Owner additions)
magda/daw/core/controllers/Binding.hpp                (target field accommodates new owners)
magda/daw/core/controllers/BindingRegistry.cpp        (snapshot + serialization)
magda/daw/core/controllers/MidiLearnCoordinator.cpp   (accept new target shapes)
magda/daw/audio/ControllerParamWriter.cpp             (resolve+write the new owners)
magda/daw/ui/components/chain/MacroKnobComponent.cpp  (Learn / Remove items)
magda/daw/ui/components/chain/ModulatorEditorPanel.cpp(Learn / Remove items on rate menu)
```

### Out-of-scope for Branch B

- Bipolar / unipolar mapping curves on the binding (use the controller's existing scaler)
- Take-over / pickup logic (existing controller behaviour applies)
- MIDI Learn for sync-division specifically as a discrete picker — sync mode reuses the same binding; the writeback does the round-to-ordinal

---

## Coordination — files both branches modify

Two files are touched by both:

1. **`magda/daw/ui/components/chain/MacroKnobComponent.cpp`**
   - Branch A adds a `Modulators` submenu under "Link to Parameter..."
   - Branch B adds `MIDI Learn` / `Remove MIDI Binding` items above `Show Automation Lane`

   No semantic overlap. Merge will resolve cleanly if both follow the section comments. If either touches `showLinkMenu()`'s skeleton (renames variables, restructures the dispatch lambda), say so in the PR so the other can rebase.

2. **`magda/daw/ui/components/chain/ModulatorEditorPanel.cpp`**
   - Branch A adds a "Link Target" picker that includes a `Modulators` submenu (mod-to-mod linking).
   - Branch B adds `MIDI Learn` / `Remove MIDI Binding` items inside `showRateSliderContextMenu()`.

   Same story — different functions, no semantic overlap.

If either branch needs to refactor the right-click menu structure (e.g. introduce a shared `buildAutomationContextMenu(target)` helper) — coordinate before doing so. Default: leave the menu code as-is and just append items.

## Build & test

- `make debug` after every meaningful change. Don't run concurrent builds.
- For Branch A: with a macro linked to an LFO rate, moving the macro should change the LFO's audible rate in both Hz mode and sync mode. Same for one mod modulating another mod's rate.
- For Branch B: with a CC bound to a macro, moving the controller fader should change the macro value in real time and respect take-over rules. Same for a CC bound to an LFO rate (Hz and sync modes).
- Project save/load: each binding/link kind round-trips without loss.
- Old projects load without crashing — legacy `MacroTarget` defaults to `Kind::DeviceParam`.

## Out-of-scope for both branches

- Modulator depth lane / depth as a binding target (future)
- Audio-rate or sample-accurate macro modulation (existing macro path is message-thread-rate)
- Cross-track linking (`MacroTarget` is single-scope today; extending that is its own branch)
