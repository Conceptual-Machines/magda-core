# Tracktion Engine Parameter Architecture Analysis

## The Problem

Tone generator frequency was wrong (360 Hz needed to produce 440 Hz sound), and parameters don't respond to UI changes.

## Root Cause

We were setting `CachedValue` directly but the audio thread reads from `AutomatableParameter.currentValue`, which wasn't being updated.

## Architecture Layers (Bottom to Top)

```
┌─────────────────────────────────────────────┐
│         Audio Thread (reads this)           │
│  AutomatableParameter.currentValue (atomic) │
└─────────────────────────────────────────────┘
                    ↑
                    │ updateFromAttachedValue()
                    │
┌─────────────────────────────────────────────┐
│         CachedValue<float>                  │
│  (Runtime cache of ValueTree property)      │
└─────────────────────────────────────────────┘
                    ↑
                    │ referTo()
                    │
┌─────────────────────────────────────────────┐
│         ValueTree (persistent storage)      │
│  state[IDs::frequency] = 440.0f             │
└─────────────────────────────────────────────┘
```

## Data Flow

### ToneGeneratorPlugin Initialization (constructor)

```cpp
// Link CachedValue to ValueTree property
frequency.referTo (state, IDs::frequency, um, 220.0f);

// Create AutomatableParameter attached to CachedValue
frequencyParam = createSuffixedParameter (*this, "frequency", ..., frequency, {});
// This calls frequencyParam->attachToCurrentValue(frequency) internally

// Add to automation system
addAutomatableParameter (frequencyParam);
```

### Audio Processing (every block)

```cpp
// ToneGeneratorPlugin::applyToBuffer()
float freq = frequencyParam->getCurrentValue();  // Reads atomic
sine.setFrequency(freq);
```

Returns: `AutomatableParameter.currentValue` (atomic float)

### Setting a Value Correctly

**Option 1: Via AutomatableParameter (UI/automation layer)**

```cpp
// This is how UI should set values:
tone->frequencyParam->setParameter(440.0f, juce::sendNotification);
```

This:
1. Updates `currentValue` atomic ✓ (audio sees it)
2. Calls `parameterChanged()` which triggers async update
3. Async update calls `value.setValue(parameter.currentValue, nullptr)` to sync CachedValue
4. ValueTree gets updated

**Option 2: Via CachedValue (state restore)**

```cpp
// When loading from file or restoring state:
tone->frequency = 440.0f;  // Updates ValueTree, syncs CachedValue
tone->frequencyParam->updateFromAttachedValue();  // Syncs parameter from CachedValue
```

This:
1. `frequency = 440.0f` updates ValueTree property
2. CachedValue caches it
3. ValueTree listener fires but does NOT update parameter (by design!)
4. `updateFromAttachedValue()` explicitly syncs parameter.currentValue ✓

### What We Were Doing Wrong

```cpp
// WRONG: Only sets CachedValue
tone->frequency = 440.0f;
```

This:
- ✓ Updates ValueTree
- ✓ Updates CachedValue
- ✗ Does NOT update `frequencyParam->currentValue`
- Audio thread still reads old value!

### Why ValueTree Changes Don't Auto-Update Parameter

From `AutomatableParameter::valueTreePropertyChanged()`:

```cpp
// When ValueTree property changes:
if (attachedValue != nullptr && attachedValue->updateIfMatches (v, i))
{
    // N.B. we shouldn't call attachedValue->updateParameterFromValue here as this
    // will set the base value of the parameter. The change in property could be due
    // to a Modifier or automation change so we don't want to force that to be the base value

    // ONLY calls forceUpdateOfCachedValue(), NOT updateParameterFromValue()!
    listeners.call (&Listener::currentValueChanged, *this);
}
```

This is by design: automation/modifiers change CachedValue, but shouldn't override the base parameter value.

## Our Architecture Problem

**Current**: DeviceProcessor directly manipulates Tracktion Engine's internal CachedValues

```
UI → TrackManager → AudioBridge → DeviceProcessor → CachedValue (WRONG)
                                                          ↓
                                                    AutomatableParameter (not synced!)
```

**Should Be**: Use parameter API, automation layer is separate

```
UI → TrackManager → AudioBridge → DeviceProcessor → AutomatableParameter.setParameter()
                                                          ↓
                                                    CachedValue (synced automatically)
```

## The Fix

### Short Term (Immediate Fix)

In `ToneGeneratorProcessor::setFrequency()`:

```cpp
void ToneGeneratorProcessor::setFrequency(float hz) {
    if (auto* tone = getTonePlugin()) {
        hz = juce::jlimit(20.0f, 20000.0f, hz);

        // Set via parameter (proper way)
        if (tone->frequencyParam) {
            tone->frequencyParam->setParameter(hz, juce::dontSendNotification);
        }
    }
}
```

### Medium Term (Proper Architecture)

1. **Remove CachedValue manipulation from DeviceProcessor**
   - Never touch `tone->frequency` directly
   - Always use `tone->frequencyParam->setParameter()`

2. **Automation layer should be separate**
   - DeviceProcessor doesn't know about AutomatableParameter
   - AudioBridge handles automation → parameter binding
   - DeviceProcessor only exposes high-level setters

3. **Parameter value flow**
   ```
   UI/Automation → AutomatableParameter → Audio Thread
                            ↓
                      CachedValue → ValueTree (persistence)
   ```

## Tracktion Engine Best Practices

1. **Audio thread reads from**: `AutomatableParameter::getCurrentValue()`
2. **UI sets via**: `AutomatableParameter::setParameter()`
3. **State restore**:
   ```cpp
   copyPropertiesToCachedValues(v, frequency, level, oscType);
   for (auto p : getAutomatableParameters())
       p->updateFromAttachedValue();
   ```
4. **Never manipulate CachedValues directly** (except in restorePluginStateFromValueTree)

## References

- `ToneGeneratorPlugin::ToneGeneratorPlugin()` - parameter setup
- `ToneGeneratorPlugin::applyToBuffer()` - how audio reads values
- `ToneGeneratorPlugin::restorePluginStateFromValueTree()` - proper state restore pattern
- `AutomatableParameter::attachToCurrentValue()` - bidirectional binding
- `AutomatableParameter::valueTreePropertyChanged()` - why ValueTree changes don't auto-update
