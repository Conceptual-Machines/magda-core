---
name: te-host-parameter-writes
description: Write values from MAGDA into a te::AutomatableParameter (slider moves, controller input, state restore, modifier sync). Covers the setParameterFromHost requirement, why setParameter silently drops host writes when modifiers are active, and what to do after a TE submodule bump. Use when adding a new DeviceProcessor, wiring a new control surface, debugging "slider does nothing when an LFO/macro is on the parameter", or rebasing the TE patch.
---

# Host writes to te::AutomatableParameter

Host code that pushes a value into a TE plugin parameter — UI sliders, controllers, project state restore, modifier-driven sync — must call **`setParameterFromHost(value, nt)`**, not `setParameter(value, nt)`.

If you call `setParameter` and any modifier is producing nonzero output on that param, your write is silently dropped.

## Why

Upstream TE commit `a2ac9e8acd1` ("Fix macro-linked parameter drift caused by plugin feedback loop") added a guard at the top of `AutomatableParameter::setParameter`:

```cpp
void AutomatableParameter::setParameter (float value, juce::NotificationType nt)
{
    if (currentModifierValue.load() != 0.0f)
        return;
    ...
}
```

The guard exists because some plugins (synths with their own internal LFOs, etc.) report their modulated value back to the host through the parameter callback. With a host-side modifier already adding on top, that creates a feedback loop and the base drifts to maximum within a few audio blocks.

But TE has no flag distinguishing "plugin echoed its modulated value" from "user moved the host slider" — so the guard drops both. Without the patch, every parameter with any modifier attached (LFO, macro, envelope) silently ignores host writes. Affects 4OSC, every TE built-in, and every external VST/AU equally.

## The patch

MAGDA's TE fork (`Conceptual-Machines/tracktion_engine`, branch `magda_minimal`) carries commit `cc7ecae8b44` which adds:

```cpp
// Header: tracktion_AutomatableParameter.h
void setParameterFromHost (float value, juce::NotificationType);
```

Implementation is the same body as `setParameter` minus the modifier guard. The original `setParameter` still exists and still drops plugin echoes — leave it for that purpose.

## How to call from MAGDA

```cpp
// Wrong — gets silently dropped when a modifier is active
params[paramIndex]->setParameter(value, juce::sendNotificationSync);

// Right
params[paramIndex]->setParameterFromHost(value, juce::sendNotificationSync);
```

## When to use which

| Caller | Method |
|---|---|
| `DeviceProcessor::setParameterByIndex` (UI slider, controller, automation playback) | `setParameterFromHost` |
| `ExternalPluginProcessor::syncFromDeviceInfo` (project state restore) | `setParameterFromHost` |
| `ControllerParamWriter` (MIDI controller) | `setParameterFromHost` |
| `RackSyncManager` / `PluginManager` macro setters, sync paths | `setParameterFromHost` |
| `applyLFOProperties` and other modifier-internal param setup | `setParameterFromHost` |
| Internal MAGDA plugin ValueTree → TE param sync (Arpeggiator, StepSequencer, MagdaSampler, DrumGrid) | `setParameterFromHost` |
| Plugin-internal callback echoing its own modulated value back to host | `setParameter` (so the guard catches it) |

The default for any new MAGDA-side write is `setParameterFromHost`. Only use `setParameter` if the call originates from inside the plugin's own callback (which MAGDA rarely writes — mostly upstream TE territory).

## Adding a new processor

If you add a new `DeviceProcessor` subclass that wraps a TE plugin:

```cpp
void MyNewProcessor::setParameterByIndex(int paramIndex, float value) {
    if (!plugin_) return;
    auto params = plugin_->getAutomatableParameters();
    if (paramIndex >= 0 && paramIndex < params.size()) {
        params[paramIndex]->setParameterFromHost(value, juce::sendNotificationSync);
    }
}
```

Match the pattern in `FourOscProcessor::setParameterByIndex` etc. in `magda/daw/audio/DeviceProcessor.cpp`.

## After a TE submodule bump

If `make debug` fails with `'setParameterFromHost' is not a member of 'tracktion::AutomatableParameter'`, the patch was lost in the bump. Re-apply:

1. `cd third_party/tracktion_engine`
2. Check the patch commit is present: `git log --oneline | grep setParameterFromHost`
3. If missing, cherry-pick `cc7ecae8b44` from the `magda_minimal` branch of the fork, or re-apply the diff:
   - Header: add `void setParameterFromHost (float value, juce::NotificationType);` next to `setParameter` in `tracktion_AutomatableParameter.h`
   - Cpp: extract everything after the guard in `setParameter` into a new `setParameterFromHost`, and replace the post-guard body of `setParameter` with `setParameterFromHost(value, nt);`
4. Commit on the fork's `magda_minimal` branch and push, then bump the parent submodule pointer.

## How to verify the regression hasn't reintroduced

Set up an LFO on a parameter (any plugin's filter cutoff is a clean test), then move the underlying parameter slider while the LFO is producing audible modulation:

- **Working:** the modulation envelope shifts up/down with the slider — you can hear the sweep range move.
- **Broken (regression):** the slider has no audible effect; the sweep stays pinned where it was.
