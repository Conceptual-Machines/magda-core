---
name: valuetree
description: JUCE ValueTree patterns as used in MAGDA. Use when working with device state (MagdaDevice::flushState/restoreState, DeviceState), ValueTree listeners, or the few remaining CachedValue bindings.
---

# JUCE ValueTree & Serialization Patterns

ValueTree is JUCE's tree-of-properties container. In MAGDA it is not the project model: the project model is plain C++ (TrackManager, ClipManager, DeviceInfo) with its own undo system. ValueTree appears at the device state boundary (`MagdaDevice::flushState`/`restoreState`, `magda/daw/core/DeviceState.*`) and in a handful of UI and agent helpers. Automatable parameters are not stored in it; `DeviceInfo::parameters` is their persisted authority.

## ValueTree Basics

### Creating and Populating

```cpp
#include <juce_data_structures/juce_data_structures.h>

// Create a tree with an Identifier type
juce::ValueTree tree(juce::Identifier("MyDevice"));

// Set properties
tree.setProperty("gain", 0.5f, nullptr);       // no undo
tree.setProperty("name", "Lead", undoManager);  // with undo

// Add child trees
juce::ValueTree child(juce::Identifier("CHAIN"));
child.setProperty("index", 0, nullptr);
tree.addChild(child, -1, nullptr);  // -1 = append at end
```

### Reading Properties

```cpp
// Read with default fallback
float gain = tree.getProperty("gain", 1.0f);
juce::String name = tree.getProperty("name", "Default");

// Check existence
if (tree.hasProperty("gain")) { /* ... */ }

// Get typed child
juce::ValueTree chainTree = tree.getChildWithName("CHAIN");
if (chainTree.isValid()) {
    int idx = chainTree.getProperty("index");
}
```

### Hierarchy Navigation

```cpp
// Parent access
juce::ValueTree parent = tree.getParent();

// Child iteration
for (int i = 0; i < tree.getNumChildren(); ++i) {
    auto child = tree.getChild(i);
    if (child.hasType("CHAIN")) {
        // process chain child
    }
}

// Range-based iteration (JUCE 7+)
for (auto child : tree) {
    DBG(child.getType().toString());
}

// Find specific child by property
for (int i = 0; i < tree.getNumChildren(); ++i) {
    auto child = tree.getChild(i);
    if (child.hasType("CHAIN") && (int)child.getProperty("index") == 2)
        return child;
}
```

### Identifier Constants

Always define Identifiers as static constants to avoid repeated string hashing:

**Header (.h):**
```cpp
class MyDevice : public MagdaDevice {
    static const juce::Identifier gainId;
    static const juce::Identifier chainId;
    static const juce::Identifier muteId;
};
```

**Source (.cpp):**
```cpp
const juce::Identifier MyDevice::gainId("gain");
const juce::Identifier MyDevice::chainId("CHAIN");
const juce::Identifier MyDevice::muteId("mute");
```

## CachedValue<T>

`CachedValue<T>` binds a C++ variable to a ValueTree property and caches the value locally. Almost none remain in `magda/`. New device code does not use it: parameters are read through the engine's resolved parameter values, and non-parameter state goes through `flushState`/`restoreState`. Do not read a CachedValue from the audio thread; the cache is updated by a listener on whichever thread writes the tree.

```cpp
juce::CachedValue<float> level;
level.referTo(tree, "level", nullptr, 1.0f);  // tree, property, undo manager, default

float v = level.get();
level = 0.75f;                                // writes the property, fires listeners
```

After replacing the tree's properties wholesale, call `forceUpdateOfCachedValue()` so the cache re-reads.

## Device State

`MagdaDevice` (`magda/daw/audio/plugins/MagdaDevice.hpp`) exchanges non-parameter state as a plain ValueTree:

```cpp
void flushState(juce::ValueTree& state) override;          // write into state
void restoreState(const juce::ValueTree& state) override;  // read it back
```

`magda/daw/core/DeviceState.hpp` defines the persisted document (schema v2): a MAGDA-owned tree with the device's own property names, no engine ids. `toValueTree(node)` in the `device_state` namespace gives the tree `restoreState` takes. Automatable parameter values are excluded from it.

```cpp
void MyDevice::flushState(juce::ValueTree& state) {
    state.setProperty(samplePathId, samplePath, nullptr);
    for (auto& step : steps) {
        juce::ValueTree t(stepId);
        t.setProperty(indexId, step.index, nullptr);
        state.addChild(t, -1, nullptr);
    }
}

void MyDevice::restoreState(const juce::ValueTree& state) {
    samplePath = state.getProperty(samplePathId, "").toString();
    steps.clear();
    for (auto t : state)
        if (t.hasType(stepId))
            steps.push_back({(int)t.getProperty(indexId, 0)});
}
```

`flushState`/`restoreState` run on the message thread. Rebuild what the audio thread reads off-thread and publish it atomically (see the audio-thread skill).

## ValueTree::Listener

### Implementing a Listener

```cpp
class MyComponent : public juce::Component,
                    private juce::ValueTree::Listener
{
public:
    MyComponent(juce::ValueTree stateToWatch)
        : watchedState(stateToWatch)
    {
        watchedState.addListener(this);
    }

    ~MyComponent() override {
        watchedState.removeListener(this);  // Always remove in destructor
    }

private:
    void valueTreePropertyChanged(juce::ValueTree& tree,
                                  const juce::Identifier& property) override
    {
        if (property == MyDevice::levelId) {
            // Update UI, but check what thread you're on!
            // If changed from audio thread, use AsyncUpdater
            triggerAsyncUpdate();
        }
    }

    void valueTreeChildAdded(juce::ValueTree& parent,
                             juce::ValueTree& child) override
    {
        // A child tree was added
    }

    void valueTreeChildRemoved(juce::ValueTree& parent,
                               juce::ValueTree& child,
                               int index) override
    {
        // A child tree was removed
    }

    juce::ValueTree watchedState;
};
```

### Thread Safety Warning

Listeners fire on the thread that made the change. Never do UI work directly in a listener that might be called from the audio thread. Use `juce::AsyncUpdater` or `juce::MessageManager::callAsync()` to bounce to the message thread.

## Undo

Pass `nullptr` as the ValueTree undo manager. Project undo is MAGDA's own `magda::UndoManager` (`magda/daw/core/UndoManager.hpp`): wrap the change in an `UndoableCommand` and run it with `UndoManager::getInstance().executeCommand(...)`, or group several with `CompoundOperationScope`. Do not mutate device state on the ValueTree with an undo manager and expect it to be undoable.

## Common Patterns in This Codebase

### Chain Properties

Devices with multiple chains (e.g., drum grid, multi-output) write per-chain state as child trees in `flushState`:

```cpp
// In flushState(state)
for (int i = 0; i < numChains; ++i) {
    juce::ValueTree chainTree("CHAIN");
    chainTree.setProperty("index", i, nullptr);
    chainTree.setProperty("level", 1.0f, nullptr);
    chainTree.setProperty("pan", 0.0f, nullptr);
    chainTree.setProperty("mute", false, nullptr);
    chainTree.setProperty("solo", false, nullptr);
    state.addChild(chainTree, -1, nullptr);
}

// In restoreState(state)
for (int i = 0; i < state.getNumChildren(); ++i) {
    auto child = state.getChild(i);
    if (child.hasType("CHAIN")) {
        float chainLevel = child.getProperty("level", 1.0f);
        float chainPan = child.getProperty("pan", 0.0f);
        bool chainMute = child.getProperty("mute", false);
    }
}
```

### Finding Child Trees by Type

```cpp
// Find a specific child tree
juce::ValueTree findChainByIndex(const juce::ValueTree& parentState, int index) {
    for (int i = 0; i < parentState.getNumChildren(); ++i) {
        auto child = parentState.getChild(i);
        if (child.hasType("CHAIN") && (int)child.getProperty("index") == index)
            return child;
    }
    return {};  // invalid ValueTree
}

// Collect all children of a type
std::vector<juce::ValueTree> getChains(const juce::ValueTree& parentState) {
    std::vector<juce::ValueTree> chains;
    for (int i = 0; i < parentState.getNumChildren(); ++i) {
        auto child = parentState.getChild(i);
        if (child.hasType("CHAIN"))
            chains.push_back(child);
    }
    return chains;
}
```

## Common Pitfalls

1. **Stale CachedValues** after replacing properties wholesale: call `forceUpdateOfCachedValue()`
2. **Touching a ValueTree from the audio thread**: listeners fire on the writing thread, and tree access is not real-time safe; publish plain data via atomics instead
3. **Not removing listeners in destructors**: dangling listener = crash
4. **String literals instead of Identifier constants**: a new Identifier is hashed on every use
5. **Passing an undo manager to `setProperty`**: pass `nullptr`; undo goes through `magda::UndoManager`
6. **Missing `isValid()` checks**: `getChildWithName()` returns an invalid ValueTree if not found
