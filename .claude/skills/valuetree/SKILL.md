---
name: valuetree
description: JUCE ValueTree and serialization patterns for Tracktion Engine plugins. Use when working with plugin state, CachedValue properties, ValueTree listeners, or state persistence/restoration.
---

# JUCE ValueTree & Serialization Patterns

ValueTree is the core data model in JUCE and Tracktion Engine. Every plugin, track, and edit stores its state as a ValueTree. This skill covers the patterns used throughout the MAGDA codebase.

## ValueTree Basics

### Creating and Populating

```cpp
#include <juce_data_structures/juce_data_structures.h>

// Create a tree with an Identifier type
juce::ValueTree tree(juce::Identifier("MyPlugin"));

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
class MyPlugin : public te::Plugin {
    static const juce::Identifier gainId;
    static const juce::Identifier chainId;
    static const juce::Identifier muteId;
};
```

**Source (.cpp):**
```cpp
const juce::Identifier MyPlugin::gainId("gain");
const juce::Identifier MyPlugin::chainId("CHAIN");
const juce::Identifier MyPlugin::muteId("mute");
```

## CachedValue<T>

`CachedValue<T>` binds a C++ variable to a ValueTree property. It caches the value locally so reads are thread-safe (no locking), making it suitable for audio-thread reads.

### Binding to a Property

```cpp
class MyPlugin : public te::Plugin {
    juce::CachedValue<float> level;
    juce::CachedValue<float> pan;
    juce::CachedValue<bool> mute;
    juce::CachedValue<bool> solo;

    MyPlugin(te::PluginCreationInfo info) : te::Plugin(info) {
        // referTo(tree, propertyId, undoManager, defaultValue)
        level.referTo(state, "level", nullptr, 1.0f);
        pan.referTo(state, "pan", nullptr, 0.0f);
        mute.referTo(state, "mute", nullptr, false);
        solo.referTo(state, "solo", nullptr, false);
    }
};
```

### Reading and Writing

```cpp
// Read — thread-safe, returns cached copy
float currentLevel = level.get();

// Can also use implicit conversion
float val = level;

// Write — updates the ValueTree property (triggers listeners)
level = 0.75f;

// Equivalent to:
state.setProperty("level", 0.75f, nullptr);
```

### Force Update After State Restore

After `restorePluginStateFromValueTree()` copies new values into the `state` tree, the CachedValues may still hold stale data. You must call `forceUpdateOfCachedValue()`:

```cpp
void restorePluginStateFromValueTree(const juce::ValueTree& v) override {
    // This copies matching properties from v into this->state
    te::copyPropertiesToCachedValues(state, v, nullptr);

    // Force each CachedValue to re-read from the tree
    level.forceUpdateOfCachedValue();
    pan.forceUpdateOfCachedValue();
    mute.forceUpdateOfCachedValue();
    solo.forceUpdateOfCachedValue();
}
```

## Serialization Pattern in Tracktion Engine Plugins

### Plugin `state` Member

Every `te::Plugin` has a `state` member which is a `juce::ValueTree`. This is the single source of truth for all plugin data.

### Full Plugin Pattern

```cpp
// Header
class MyPlugin : public te::Plugin {
public:
    MyPlugin(te::PluginCreationInfo);
    ~MyPlugin() override;

    static const char* xmlTypeName;

    // te::Plugin overrides
    void restorePluginStateFromValueTree(const juce::ValueTree&) override;
    juce::String getName() const override { return "My Plugin"; }

private:
    static const juce::Identifier levelId;
    static const juce::Identifier panId;
    static const juce::Identifier chainTreeId;

    juce::CachedValue<float> level;
    juce::CachedValue<float> pan;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MyPlugin)
};

// Source
const juce::Identifier MyPlugin::levelId("level");
const juce::Identifier MyPlugin::panId("pan");
const juce::Identifier MyPlugin::chainTreeId("CHAIN");

MyPlugin::MyPlugin(te::PluginCreationInfo info)
    : te::Plugin(info)
{
    // Bind CachedValues to state
    level.referTo(state, levelId, nullptr, 1.0f);
    pan.referTo(state, panId, nullptr, 0.0f);

    // Initialize child trees if not present
    if (!state.getChildWithName(chainTreeId).isValid()) {
        juce::ValueTree chainTree(chainTreeId);
        chainTree.setProperty("index", 0, nullptr);
        state.addChild(chainTree, -1, nullptr);
    }
}

MyPlugin::~MyPlugin() {
    notifyListenersOfDeletion();
}

void MyPlugin::restorePluginStateFromValueTree(const juce::ValueTree& v) {
    te::copyPropertiesToCachedValues(state, v, nullptr);

    level.forceUpdateOfCachedValue();
    pan.forceUpdateOfCachedValue();

    // Restore child trees
    for (int i = state.getNumChildren(); --i >= 0;)
        state.removeChild(i, nullptr);

    for (auto child : v) {
        state.addChild(child.createCopy(), -1, nullptr);
    }
}
```

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
        if (property == MyPlugin::levelId) {
            // Update UI — but check what thread you're on!
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

Listeners fire on the thread that made the change. If a property is set from the audio thread, the listener callback runs on the audio thread. Never do UI work directly in a listener that might be called from the audio thread. Use `juce::AsyncUpdater` or `juce::MessageManager::callAsync()` to bounce to the message thread.

## UndoManager Integration

```cpp
// With undo support (UI-driven changes)
state.setProperty("gain", newValue, &edit.getUndoManager());
state.addChild(child, -1, &edit.getUndoManager());

// Without undo (initialization, audio-thread, internal state)
state.setProperty("gain", newValue, nullptr);
state.addChild(child, -1, nullptr);

// In TE plugins, get the undo manager via:
auto* um = getUndoManager();
state.setProperty("gain", newValue, um);
```

## Common Patterns in This Codebase

### Chain Properties

Plugins with multiple chains (e.g., drum grid, multi-output) store per-chain state as child trees:

```cpp
// Setting up chain state
for (int i = 0; i < numChains; ++i) {
    juce::ValueTree chainTree("CHAIN");
    chainTree.setProperty("index", i, nullptr);
    chainTree.setProperty("level", 1.0f, nullptr);
    chainTree.setProperty("pan", 0.0f, nullptr);
    chainTree.setProperty("mute", false, nullptr);
    chainTree.setProperty("solo", false, nullptr);
    state.addChild(chainTree, -1, nullptr);
}

// Reading chain state
for (int i = 0; i < state.getNumChildren(); ++i) {
    auto child = state.getChild(i);
    if (child.hasType("CHAIN")) {
        float chainLevel = child.getProperty("level", 1.0f);
        float chainPan = child.getProperty("pan", 0.0f);
        bool chainMute = child.getProperty("mute", false);
    }
}
```

### CachedValue Arrays for Chains

When you need audio-thread-safe reads for multiple chains:

```cpp
struct ChainState {
    juce::CachedValue<float> level;
    juce::CachedValue<float> pan;
    juce::CachedValue<bool> mute;
    juce::CachedValue<bool> solo;

    void referTo(juce::ValueTree& chainTree) {
        level.referTo(chainTree, "level", nullptr, 1.0f);
        pan.referTo(chainTree, "pan", nullptr, 0.0f);
        mute.referTo(chainTree, "mute", nullptr, false);
        solo.referTo(chainTree, "solo", nullptr, false);
    }

    void forceUpdate() {
        level.forceUpdateOfCachedValue();
        pan.forceUpdateOfCachedValue();
        mute.forceUpdateOfCachedValue();
        solo.forceUpdateOfCachedValue();
    }
};

std::vector<ChainState> chainStates;
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

1. **Forgetting `forceUpdateOfCachedValue()`** after `restorePluginStateFromValueTree()` — CachedValues will hold stale data
2. **Setting properties from the audio thread** — Triggers listeners on the audio thread; use CachedValue for reads, avoid writes from audio thread when possible
3. **Not removing listeners in destructors** — Dangling listener = crash
4. **Using string literals instead of Identifier constants** — Creates a new Identifier each time, wastes CPU on hashing
5. **Passing `undoManager` during initialization** — Use `nullptr` for initial setup; only pass undoManager for user-driven changes
6. **Forgetting `isValid()` checks** — `getChildWithName()` returns an invalid ValueTree if not found; always check before use
