---
name: audio-thread
description: Audio thread safety and lock-free programming patterns for JUCE/Tracktion Engine. Use when writing Plugin::applyToBuffer(), real-time audio callbacks, metering, or any code that touches the audio thread. Covers what is forbidden, lock-free communication, and codebase-specific patterns.
---

# Audio Thread Safety & Lock-Free Patterns

The audio thread in JUCE/Tracktion Engine runs under strict real-time constraints. Any blocking or unbounded operation causes audible glitches (clicks, dropouts, silence). This skill covers what is forbidden, how to communicate safely between threads, and the patterns used in this codebase.

## What Is Forbidden on the Audio Thread

All of the following can block, allocate, or take unbounded time. Never do any of these inside `Plugin::applyToBuffer()`, `AudioProcessor::processBlock()`, or any code called from the audio callback.

### 1. Memory Allocation

```cpp
// FORBIDDEN - all of these may call malloc/new
auto* p = new MyObject();                  // heap allocation
std::vector<float> temp(numSamples);       // heap allocation on resize
juce::String s = "level: " + juce::String(value);  // heap allocation
juce::Array<int> arr;
arr.add(42);                               // may reallocate
myStdVector.push_back(x);                  // may reallocate
```

**Instead:** Pre-allocate in `initialise()` or use stack-allocated fixed-size buffers.

```cpp
// OK - stack allocation with known size
float temp[2048];

// OK - pre-allocated in initialise(), reused in applyToBuffer()
std::vector<float> scratchBuffer;  // member variable, resized in initialise()
```

### 2. Locks and Mutexes

```cpp
// FORBIDDEN - may block waiting for another thread
juce::ScopedLock sl(criticalSection);
std::lock_guard<std::mutex> lock(mutex);
std::unique_lock<std::mutex> ul(mutex);
```

**Instead:** Use atomics, lock-free queues, or `juce::AbstractFifo`.

### 3. MessageManager Calls

```cpp
// FORBIDDEN - posts to message thread, may allocate, may block
juce::MessageManager::callAsync([this] { /* ... */ });
sendChangeMessage();
triggerAsyncUpdate();  // OK only from non-audio thread
```

### 4. File I/O

```cpp
// FORBIDDEN - unbounded latency
juce::File("x.wav").loadFileAsData();
fopen(), fread(), fwrite();
DBG("value: " + juce::String(x));  // writes to stderr
```

### 5. Objective-C Message Sends (macOS)

```cpp
// FORBIDDEN - ObjC runtime may take locks
// Any call that crosses into ObjC (NSLog, CoreFoundation bridged calls, etc.)
```

### 6. Unbounded Operations

```cpp
// FORBIDDEN - unknown iteration count
while (!ready.load()) { /* spin */ }  // unbounded spin
for (auto& item : dynamicContainer) { /* ... */ }  // if container can grow
```

## Lock-Free Communication Patterns

### Pattern 1: std::atomic for Simple Values

Use for single values shared between audio and UI threads. `memory_order_relaxed` is sufficient when there is no ordering dependency between multiple variables.

```cpp
class MyPlugin : public te::Plugin {
    std::atomic<float> gainLevel{1.0f};

    // Message thread (UI/parameter change):
    void setGain(float g) {
        gainLevel.store(g, std::memory_order_relaxed);
    }

    // Audio thread:
    void applyToBuffer(const PluginRenderContext& rc) override {
        float g = gainLevel.load(std::memory_order_relaxed);
        rc.destBuffer->applyGain(0, rc.bufferNumSamples, g);
    }
};
```

### Pattern 2: Atomic Exchange for Peak Metering

The audio thread writes a running maximum. The UI thread reads and resets in one atomic operation, ensuring no peaks are lost and no lock is needed.

```cpp
class MyPlugin : public te::Plugin {
    std::atomic<float> peakLeft{0.0f};
    std::atomic<float> peakRight{0.0f};

    // Audio thread: update running peak
    void applyToBuffer(const PluginRenderContext& rc) override {
        auto& buf = *rc.destBuffer;
        float newPeakL = buf.getMagnitude(0, rc.bufferStartSample, rc.bufferNumSamples);
        float newPeakR = buf.getMagnitude(1, rc.bufferStartSample, rc.bufferNumSamples);

        // Only store if new peak is greater than current
        auto prevL = peakLeft.load(std::memory_order_relaxed);
        if (newPeakL > prevL)
            peakLeft.store(newPeakL, std::memory_order_relaxed);

        auto prevR = peakRight.load(std::memory_order_relaxed);
        if (newPeakR > prevR)
            peakRight.store(newPeakR, std::memory_order_relaxed);
    }

    // UI thread (timer callback): read and reset
    float consumePeakLeft() {
        return peakLeft.exchange(0.0f, std::memory_order_relaxed);
    }
    float consumePeakRight() {
        return peakRight.exchange(0.0f, std::memory_order_relaxed);
    }
};
```

### Pattern 3: juce::CachedValue for ValueTree-Backed Properties

`CachedValue<T>` caches a ValueTree property in a local variable that is safe to read on the audio thread (the cached copy is updated on the message thread via listener, but the read is just a plain member access).

```cpp
class MyPlugin : public te::Plugin {
    juce::CachedValue<float> levelParam;
    juce::CachedValue<float> panParam;

    void initialise(const PluginInitialisationInfo&) override {
        // Bind to ValueTree properties (message thread)
        levelParam.referTo(state, IDs::level, nullptr, 0.8f);
        panParam.referTo(state, IDs::pan, nullptr, 0.0f);
    }

    void applyToBuffer(const PluginRenderContext& rc) override {
        // Safe to read cached value on audio thread
        float level = levelParam.get();
        float pan = panParam.get();
        // ... apply to buffer ...
    }
};
```

### Pattern 4: std::atomic<bool> for Flags and Triggers

Use for one-shot triggers (e.g., pad hit, reset signal) or boolean state flags.

```cpp
class DrumPad {
    std::atomic<bool> triggered{false};

    // UI/MIDI thread: fire trigger
    void hit() {
        triggered.store(true, std::memory_order_relaxed);
    }

    // Audio thread: consume trigger
    void processBlock(juce::AudioBuffer<float>& buffer) {
        if (triggered.exchange(false, std::memory_order_relaxed)) {
            // Start sample playback from beginning
            playbackPosition = 0;
        }
        // ... render audio ...
    }
};
```

### Pattern 5: juce::AbstractFifo / Lock-Free Queues

Use when you need to pass variable-sized data or multiple messages between threads.

```cpp
class MeterBridge {
    juce::AbstractFifo fifo{512};
    std::array<float, 512> buffer{};

    // Audio thread: write meter values
    void pushMeterValue(float value) {
        const auto scope = fifo.write(1);
        if (scope.blockSize1 > 0)
            buffer[(size_t)scope.startIndex1] = value;
    }

    // UI thread: read meter values
    float popMeterValue() {
        float result = 0.0f;
        const auto scope = fifo.read(1);
        if (scope.blockSize1 > 0)
            result = buffer[(size_t)scope.startIndex1];
        return result;
    }
};
```

## Tracktion Engine Specifics

### Plugin Lifecycle & Threading

```
Message Thread                    Audio Thread
──────────────                    ────────────
Plugin::initialise()              Plugin::applyToBuffer()
Plugin::deinitialise()              (called every audio block)
Plugin::restorePluginStateFromValueTree()
ValueTree listeners fire
```

- `Plugin::applyToBuffer(const PluginRenderContext& rc)` runs on the **audio thread**.
- `Plugin::initialise()` and `Plugin::deinitialise()` run on the **message thread**.
- Never access `Edit&`, `ValueTree`, or `UndoManager` from `applyToBuffer()`. Use `CachedValue` instead.

### PluginRenderContext Quick Reference

```cpp
void applyToBuffer(const PluginRenderContext& rc) override {
    auto& audio = *rc.destBuffer;          // juce::AudioBuffer<float>&
    auto& midi  = *rc.bufferForMidiMessages; // MidiMessageArray&
    int startSample = rc.bufferStartSample;
    int numSamples  = rc.bufferNumSamples;
    double sampleRate = sampleRateValue;   // from initialise()
}
```

### Rack Wrapping

When a plugin is rack-wrapped (inserted into a RackType), Tracktion Engine manages the audio routing. The plugin's `applyToBuffer()` is still called on the audio thread, but the buffer routing is handled by the rack. Initialise/deinitialise lifecycle is managed by the rack's node graph.

## Common Patterns in This Codebase

### Per-Chain Peak Metering

```cpp
// In a multi-chain plugin (e.g., drum grid with multiple output chains):
struct Chain {
    std::atomic<float> peak{0.0f};
    // ... other chain state ...
};

std::array<Chain, 16> chains;

// Audio thread: update peak for each chain
void applyToBuffer(const PluginRenderContext& rc) override {
    for (int i = 0; i < numActiveChains; ++i) {
        float mag = getChainMagnitude(i, rc);
        auto prev = chains[i].peak.load(std::memory_order_relaxed);
        if (mag > prev)
            chains[i].peak.store(mag, std::memory_order_relaxed);
    }
}

// UI thread: consume peaks for meter display
float consumePeak(int chainIndex) {
    return chains[chainIndex].peak.exchange(0.0f, std::memory_order_relaxed);
}
```

### Pad Trigger Flags

```cpp
// Array of atomic trigger flags, one per pad
std::array<std::atomic<bool>, 16> padTriggers{};

// MIDI/UI thread
void triggerPad(int padIndex) {
    padTriggers[padIndex].store(true, std::memory_order_relaxed);
}

// Audio thread
void applyToBuffer(const PluginRenderContext& rc) override {
    for (int i = 0; i < 16; ++i) {
        if (padTriggers[i].exchange(false, std::memory_order_relaxed)) {
            startPadPlayback(i);
        }
    }
}
```

### CachedValue for Level/Pan

```cpp
// Backed by ValueTree so values persist and can be automated
juce::CachedValue<float> level, pan;

void initialise(const PluginInitialisationInfo& info) override {
    level.referTo(state, IDs::level, nullptr, 0.8f);
    pan.referTo(state, IDs::pan, nullptr, 0.0f);
}

void applyToBuffer(const PluginRenderContext& rc) override {
    float l = level.get();
    float p = pan.get();
    // Apply gain and panning to buffer...
}
```

## Debugging Checklist

If you hear clicks, dropouts, or glitches:

1. **Search for allocations** in `applyToBuffer()` - look for `new`, `String`, `Array`, `vector` operations
2. **Search for locks** - grep for `ScopedLock`, `lock_guard`, `CriticalSection` in audio path
3. **Check for DBG()** calls in audio code - these do file I/O
4. **Verify pre-allocation** - all buffers sized in `initialise()`, not in `applyToBuffer()`
5. **Check CachedValue usage** - ensure `referTo()` is called in `initialise()`, not in `applyToBuffer()`
