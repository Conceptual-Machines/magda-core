---
name: macros-mods
description: Guide for implementing and debugging macro and mod (LFO/envelope) linking on device parameters. Use when adding macro/mod support to a plugin, fixing macro link issues, or understanding the modulation system.
---

# Macros & Mods System

## Architecture Overview

Macros and mods (LFOs, envelopes, random) are modulation sources that can be linked to device parameters. The system has three layers:

1. **Data layer** — `MacroInfo`/`ModInfo` in `core/MacroInfo.hpp` and `core/ModInfo.hpp`
2. **UI layer** — `MacroPanelComponent`/`ModsPanelComponent` in `DeviceSlotComponent`
3. **Audio layer** — `PluginManager::syncDeviceMacros()`/`syncDeviceModifiers()` wires TE's `MacroParameter`/`LFOModifier` to plugin `AutomatableParameter`s

## Key Types

### MacroTarget (`core/MacroInfo.hpp`)
```cpp
struct MacroTarget {
    DeviceId deviceId;
    int paramIndex;  // Index into plugin's getAutomatableParameters()
};
```

### MacroInfo
```cpp
struct MacroInfo {
    juce::String name;
    float value = 0.5f;         // Knob position 0..1
    std::vector<MacroLink> links; // Each link has a MacroTarget + amount
};
```

### ModTarget (`core/ModInfo.hpp`)
Same structure as MacroTarget — `{deviceId, paramIndex}`.

## How Macro Linking Works

### 1. Parameter Discovery
- `DeviceProcessor::populateParameters()` fills `DeviceInfo::parameters`
- `TrackManager::updateDeviceParameters()` stores them
- `DeviceSlotComponent::getDeviceParamNames()` reads `device_.parameters` for the UI dropdown

### 2. Link Creation (UI)
- User opens macro panel → selects a target param from dropdown
- `DeviceSlotComponent::onMacroTargetChangedInternal()` → `TrackManager::setDeviceMacroTarget()`
- Stores `MacroTarget{deviceId, paramIndex}` in `MacroInfo::links`

### 3. Audio-Thread Application (`PluginManager::syncDeviceMacros()`)
```
MacroInfo.value → te::MacroParameter → param->addModifier(macroParam, link.amount)
```
- Creates a TE `MacroParameter` for each macro
- For each link, finds the target plugin via `syncedDevices_[deviceId].plugin`
- Gets `plugin->getAutomatableParameters()[paramIndex]`
- Calls `param->addModifier(*macroParam, link.amount)`
- TE handles the audio-rate modulation internally

## Making a Plugin Macro-Linkable

### Requirements
A plugin must have **AutomatableParameters** registered with TE for macros to link. CachedValues alone won't work — `getAutomatableParameters()` would return empty.

### Pattern: CachedValue + AutomatableParameter (e.g. ArpeggiatorPlugin)

1. **Register AutomatableParameters** in the constructor using `addParam()`:
```cpp
gateParam = addParam("gate", "Gate", {0.01f, 1.0f});
gateParam->setParameter(gate.get(), juce::dontSendNotification);
```

2. **Sync CachedValue → AutomatableParam** when UI changes values:
```cpp
// Use a ValueTree::Listener to push CachedValue changes to AutomatableParam
state.addListener(&paramSyncListener_);

void syncParamFromProperty(const juce::Identifier& property) {
    if (property == ArpIDs::gate && gateParam)
        gateParam->setParameter(gate.get(), juce::dontSendNotification);
}
```

3. **Read from AutomatableParam in applyToBuffer** (includes modulation):
```cpp
float gateVal = gateParam ? gateParam->getCurrentValue() : gate.get();
```

4. **Create a DeviceProcessor** in `DeviceProcessor.hpp/.cpp`:
```cpp
class MyProcessor : public DeviceProcessor {
    int getParameterCount() const override;
    ParameterInfo getParameterInfo(int index) const override;
    void populateParameters(DeviceInfo& info) const override;
    void setParameterByIndex(int paramIndex, float value);
    float getParameterByIndex(int paramIndex) const;
};
```

5. **Register in PluginManager::loadDeviceAsPlugin()**:
```cpp
processor = std::make_unique<MyProcessor>(device.id, plugin);
```

### Pattern: Native TE Params (e.g. CompressorPlugin, EqualiserPlugin)
These already have AutomatableParameters. Just create a `DeviceProcessor` subclass that wraps `getAutomatableParameters()`.

## Mod (LFO/Envelope) System

Same target structure (`ModTarget = {deviceId, paramIndex}`), but modulation source is an LFO or envelope modifier instead of a knob.

### Key differences from macros:
- Mods are TE `LFOModifier` objects attached to `plugin->getModifierList()` or `rackType->getModifierList()`
- `syncDeviceModifiers()` handles wiring
- LFO retrigger: track-level mods get MIDI via TE's `createModifierNodeForList()`, rack-level need explicit `addConnection(rackIOId, 0, modifier->itemID, 0)`

## UI Visibility

In `DeviceSlotComponent`:
- `modButton_` / `macroButton_` visibility controlled in `resizedContent()`, `resizedHeaderExtra()`, `resizedCollapsed()`, and `createCustomUI()`
- MIDI devices (`DeviceType::MIDI`) typically hide mods; macros can be shown selectively
- DrumGrid hides both

## Debugging

- Check `device_.parameters` is populated (log in `getDeviceParamNames()`)
- Verify `getAutomatableParameters().size()` matches expected param count
- Check `syncDeviceMacros()` runs after link changes (breakpoint or DBG)
- Ensure `paramIndex` in `MacroTarget` matches the index in `getAutomatableParameters()`
