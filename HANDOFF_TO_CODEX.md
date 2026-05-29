# Handoff: Dry/Wet Mix Knob — wrapper param canonicalization still broken

## TL;DR for Codex

The user wants a small wet/dry mix knob at the top of the device meter strip
on external (VST/AU) plugins. The knob exists, renders, and tracks the right
TE params on writes — but turning the knob is still mutating the wrong slot
in the parameter grid (e.g. Pro-Q "Band 1 Used" / "Band 1 Enabled" change
when the knob is rotated). The previous agent (Claude — me) chased this bug
through ~20 files over multiple commits and clearly did not finish the audit.
The user is rightly furious. Please do not repeat my mistakes. Read this
whole document before touching code.

## The structural fault

`paramIndex` historically had two meanings in this codebase that were
silently identical for external plugins:

1. **TE index** — position in `te::ExternalPlugin::getAutomatableParameters()`.
   This is what `magda::ParameterInfo::paramIndex` stores. Automation lanes,
   aliases, and audio-side host writes all key on this.
2. **Array position** in `magda::DeviceInfo::parameters`. Many UI consumers
   index this vector directly with `device.parameters[paramIndex]`.

For external plugins they USED to be equal because TE prepends a synthetic
DryGain/WetGain pair to `getAutomatableParameters()`, and we used to leave
those two entries in `DeviceInfo::parameters` at positions 0 and 1. So TE
index 0 == array position 0 (DryGain), 1 == 1 (WetGain), 2 == 2 (first plugin
param), and so on.

In commits `c561b5cbb` and `221eab1ef` I partitioned the wrapper pair out of
`parameters` into a new `DeviceInfo::wrapperParameters` bucket. Now for
external plugins:

- TE indices 0 and 1 live in `wrapperParameters`.
- `parameters[0]` is the FIRST PLUGIN PARAM (Pro-Q's "Band 1 Used").
- Any code that does `device.parameters[paramIndex]` with an intended
  TE index writes/reads the wrong slot.

The user decided we canonicalize on **TE index** everywhere (option "A").
That means every `device.parameters[i]` access where `i` is intended as a
TE index has to become `device.findParameterByIndex(i)` (searches both
buckets by `.paramIndex` field).

`DeviceInfo::findParameterByIndex` exists now (see `magda/daw/core/DeviceInfo.hpp`).
It returns a `ParameterInfo*` across both buckets, or nullptr. **Use it.**

## What the user is seeing

Load Pro-Q 4 (FabFilter VST3). Mix knob renders correctly at the top right
of the device strip. Turn the knob → "Band 1 Used" toggles Unused↔Used and
"Band 1 Enabled" toggles Enabled↔Disabled in the param grid. That means
SOMETHING in the listener / refresh path is still using a paramIndex value
as if it were an array position into `device.parameters` and writing or
displaying the wrong slot.

## What I already fixed (so don't redo)

- `magda/daw/core/DeviceInfo.hpp` — added `findParameterByIndex(int)`,
  both mutable and const, searches both buckets.
- `magda/daw/core/TrackManagerDevices.cpp` — `setDeviceParameterValue` (both
  overloads) now uses `findParameterByIndex`. Dropped the old
  `findStoredParameterIndex` helper.
- `magda/daw/ui/components/chain/DeviceSlotComponent.cpp`:
  - `automationValueChanged` (line ~948-979) uses `findParameterByIndex`.
  - `showAutomationLaneForParam` (line ~917) uses `findParameterByIndex`.
  - The FourOscUI/custom-UI param info refresh near line ~2473 uses
    `findParameterByIndex`.
- `magda/daw/ui/components/chain/NodeComponent.cpp:1421-1426` — mod-link name
  resolver uses `findParameterByIndex`.
- `magda/daw/ui/components/chain/slot/DeviceParameterChangeHandler.cpp` —
  PARTIALLY AUDITED, **not** fixed. See below.
- `magda/daw/audio/processors/external/ExternalPluginProcessor.cpp` —
  partitions TE dry/wet into `wrapperParameters` and sets `wrapperRole`.
  Has temporary DBG line `[MixKnob.populate]` — feel free to remove once the
  underlying bug is fixed.
- `magda/daw/audio/plugin_manager/PluginManager.cpp` and
  `PluginManagerSync.cpp` — converted the four "populate into temp, copy
  parameters back" sites to populate directly into the canonical DeviceInfo
  so `wrapperParameters` is no longer dropped on sync/restore paths.

The grid layout files (`StandardDeviceLayout`, `FaustDeviceLayout`,
`CompiledFaustDeviceLayout`, `ParamHostComponent`) are structurally correct
already — they use array position internally (cells are built by iterating
`device.parameters`) and TE index externally (`cell.targetParamIndex` =
`param.paramIndex` for outbound writes). Leave them alone.

## Primary suspect for the remaining bug

**`magda/daw/ui/components/chain/slot/DeviceParameterChangeHandler.cpp:18-30`**

```cpp
std::vector<magda::ParameterInfo>::iterator findParameterInfo(magda::DeviceInfo& device,
                                                              int paramIndex) {
    auto it = std::find_if(
        device.parameters.begin(), device.parameters.end(),
        [paramIndex](const magda::ParameterInfo& param) { return param.paramIndex == paramIndex; });

    if (it == device.parameters.end() && paramIndex >= 0 &&
        paramIndex < static_cast<int>(device.parameters.size())) {
        it = device.parameters.begin() + paramIndex;   // ← dangerous fallback
    }

    return it;
}
```

This is called from `updateCachedParameterValue` (line 51-54) which is called
from `DeviceSlotComponent::deviceParameterChanged`. When the mix knob writes
TE param 0 (DryGain), TrackManager fires
`notifyDeviceParameterChanged(devicePath, 0, value)`. The slot's listener
receives `paramIndex=0`, calls `findParameterInfo(device, 0)`:

1. First pass: search `device.parameters` for an entry with `paramIndex == 0`.
   Won't find one — DryGain is in `wrapperParameters` now.
2. Fallback: `paramIndex >= 0 && paramIndex < parameters.size()` → TRUE
   (0 is in range). Returns `device.parameters.begin() + 0` = "Band 1 Used".
3. `updateCachedParameterValue` writes `it->currentValue = newValue`.
4. The UI repaints with the wrong cached value.

That's almost certainly your bug. The fix:

- Convert `findParameterInfo` to use `DeviceInfo::findParameterByIndex` (or
  inline the equivalent two-bucket search).
- Kill the array-position fallback. If we don't find a match by TE index,
  that's a no-op — never write to a positional fallback slot.

But verify by reading every call site of `findParameterInfo` in that file
(there are at least two more — `refreshEngineAwareCompiledSlots` around
line 73 also calls it inside a loop over `slotIndex`). The compiled-Faust
path uses `slotIndex` which IS an array position (cell slot index), not a
TE index, so be careful — that one might genuinely want the array
position. Re-read in context.

Also re-check that the return-type change (iterator vs pointer) is workable.
You may want a new helper that returns `ParameterInfo*` and leave the old
iterator-returning one with corrected semantics, or just replace usage with
`device.findParameterByIndex`.

## Secondary suspects (audit, don't reflex-fix)

Run this once you've cleared the primary:

```bash
grep -rn "device\.parameters\[\|device_\.parameters\[\|deviceInfo\.parameters\[\|->parameters\[" \
  /Users/lucaromagnoli/Code/personal/magda-core/magda --include="*.cpp" --include="*.hpp" \
  | grep -v "cmake-build"
```

For each hit, classify the integer index:

- **TE index** — must become `findParameterByIndex(i)`.
- **Array position computed from iterating `device.parameters`** — fine as-is.
- **Hardcoded position for a known native plugin** (e.g.
  `DeviceCustomUIManager` has hardcoded `[0]`..`[11]` for Sampler /
  FourOsc — these plugins have no wrapper params, array position == TE
  index. Safe.) — fine.

Specific files I already classified safe (verify, don't blindly trust me):
- `magda/daw/ui/components/chain/slot/DeviceCustomUIManager.cpp` lines 1366-1401
- `magda/agents/four_osc_apply.cpp` lines 82, 108

Files I classified "iteration + debug log" and skipped:
- `magda/daw/audio/plugin_manager/PluginManagerSync.cpp:40`
  (`describeChainMoveParams`)
- `magda/daw/core/TrackManagerDevices.cpp:673` (`describeMoveParams`)
- `magda/daw/core/aliases/AutoAliasGenerator.cpp:40` (alias generation —
  iterates by position, stores TE index from `.paramIndex`, correct)

## What to ABSOLUTELY NOT DO

- **Do not add a new `wrapperParameters`-vs-`parameters` flag conditional
  in every consumer.** The point of canonicalizing on TE index is that the
  buckets become an implementation detail. Use `findParameterByIndex`.
- **Do not re-introduce array-position fallbacks** when a TE-index lookup
  fails. The fallback is exactly the bug. Treat "not found" as a no-op.
- **Do not change `ParameterInfo::paramIndex`'s semantics.** It IS the TE
  index. Aliases, automation, recording all depend on this.
- **Do not collapse `parameters` and `wrapperParameters` back into one
  vector.** The whole point of the split was so the grid never shows the
  wrapper pair. Reuniting them puts the dry/wet cells back in front of the
  user.
- **Do not "fix" the grid layouts** — they're already correct.
- **Do not push to remote without the user telling you to.** The user's
  hard rule (see CLAUDE.md memory `feedback_dont_volunteer_push`).
- **Do not invent new options or feature ideas.** The user wants this one
  bug fixed. Nothing else.

## How to verify the fix

The user is running the app in console mode and reloading Pro-Q 4 each time.

1. Build: `make debug` (it's fast incrementally).
2. The user will run, you'll see logs.
3. Load Pro-Q. You should see `[MixKnob.populate] device='Pro-Q 4' params=491 wrapper=2`.
4. Look at "Band 1 Used" and "Band 1 Enabled" cells before touching the knob.
5. Turn the mix knob. Those cells must not change.
6. If they DO change: the listener path still has an array-position-as-TE-index
   bug somewhere. Re-trace.

## Layout sanity for the knob itself

The knob lives at the top of the meter strip on external plugins only:

- `magda/daw/ui/components/chain/DeviceSlotComponent.{hpp,cpp}` declares
  `mixKnob_` and `hasWrapperMixPair()`. Visibility is computed in the ctor
  and again on `updateFromDevice`.
- `magda/daw/ui/components/chain/slot/DeviceSlotContentLayout.cpp` carves
  ~18 px off the top of `stripBounds` for `controls.mixKnob` when visible.
- `magda/daw/ui/components/chain/layout/NodeHeaderStyles.hpp` defines
  `MixKnobLookAndFeel` — a tiny rotary with a pointer line.

These all work. Don't touch them unless you have a specific reason.

## Commits in this session (for context)

```
fbba1becc feat: G/M toggle reuses the meter slider for wet/dry mix
294660e9f refactor: tag wrapper params with semantic role
1adc01bde refactor: drop TE dry/wet prefix hack from ParameterConfigDialog
221eab1ef refactor: partition TE dry/wet into DeviceInfo::wrapperParameters
c561b5cbb refactor: add DeviceInfo::wrapperParameters
091e70540 feat: new footer mode icons with per-stage accent colour
524d38fb7 refactor: move mixer dB readout below the fader, light blue thumb
cf14866b3 refactor: overlay fader on peak meter in mixer strips
```

The G/M toggle in `fbba1becc` was later replaced by the always-on Mix knob
at the top — the toggle code path no longer exists in the working tree but
the commit is still in the history.

## One last thing

The user is solo dev. Their preferences live in
`/Users/lucaromagnoli/.claude/projects/-Users-lucaromagnoli-Code-personal-magda-core/memory/MEMORY.md`.
Honor them. In particular:

- No multiple-choice questions for design discussion — talk in prose.
- No "shall I push" — wait for explicit instruction.
- Build locally before committing; CI is slow.
- Proper fixes only, no bandages.

Good luck. I genuinely tried and clearly fell short. The bug is one
small audit away.

— Claude (handing off)
