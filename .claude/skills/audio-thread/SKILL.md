---
name: audio-thread
description: Audio thread safety and lock-free programming patterns for MAGDA's native engine. Use when writing MagdaDevice::process(), CompiledEffect hooks, real-time audio callbacks, metering, or any code that touches the audio thread. Covers what is forbidden, lock-free communication, and codebase-specific patterns.
---

# Audio Thread Safety & Lock-Free Patterns

The audio thread in MAGDA's native engine (`magda/engine/`) runs under strict real-time constraints. Any blocking or unbounded operation causes audible glitches (clicks, dropouts, silence). This skill covers what is forbidden, how to communicate safely between threads, and the patterns used in this codebase.

## What Is Forbidden on the Audio Thread

All of the following can block, allocate, or take unbounded time. Never do any of these inside `MagdaDevice::process()`, `AudioProcessor::processBlock()`, or any code called from the audio callback.

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

**Instead:** Pre-allocate in `prepare()` or use stack-allocated fixed-size buffers.

```cpp
// OK - stack allocation with known size
float temp[2048];

// OK - pre-allocated in prepare(), reused in process()
std::vector<float> scratchBuffer;  // member variable, resized in prepare()
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
class MyDevice : public MagdaDevice {
    std::atomic<float> gainLevel{1.0f};

    // Message thread (UI/state change):
    void setGain(float g) {
        gainLevel.store(g, std::memory_order_relaxed);
    }

    // Audio thread:
    void process(DeviceProcessContext& ctx) override {
        float g = gainLevel.load(std::memory_order_relaxed);
        for (int c = 0; c < ctx.audio.numChannels(); ++c)
            juce::FloatVectorOperations::multiply(ctx.audio.channel(c), g, ctx.numSamples());
    }
};
```

### Pattern 2: Atomic Exchange for Peak Metering

The audio thread writes a running maximum. The UI thread reads and resets in one atomic operation, ensuring no peaks are lost and no lock is needed.

```cpp
class MyDevice : public MagdaDevice {
    std::atomic<float> peakLeft{0.0f};
    std::atomic<float> peakRight{0.0f};

    // Audio thread: update running peak
    void process(DeviceProcessContext& ctx) override {
        auto buf = juceAudio(ctx);  // non-owning juce::AudioBuffer over the host's channel pointers
        float newPeakL = buf.getMagnitude(0, 0, ctx.numSamples());
        float newPeakR = buf.getMagnitude(1, 0, ctx.numSamples());

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

### Pattern 3: Resolved Parameter Values

Do not read a ValueTree or CachedValue on the audio thread. Automatable parameters reach a device already resolved: the engine combines stored value, automation and modulation (`magda/engine/param/ParamResolve.hpp`) and hands the device one value stream per parameter. A device writes the values it is given into its DSP at the top of `process()`; `devices::faust::CompiledEffect::process()` does this in `writeZones()` before `compute()`.

`sdk::Device::setParameterValue(slot, normalized)` is the host's per-block write (`setParameterSegments` for a sample-accurate one). Store it in a `std::atomic<float>` (as `CompiledFaustInterface` does) and read that on the audio thread.

Non-parameter state (sample paths, sequencer steps) comes in through `restoreState(const sdk::StateNode&)` on the message thread. Build the new data off-thread and swap it in atomically (a pointer exchange or an `AbstractFifo` message), never edit the structure the audio thread is iterating.

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

## Native Engine Specifics

### Device Lifecycle & Threading

`sdk::Device` (magda-sdk, `magda/sdk/device/Device.hpp`; docs/device-interface.md there) is the JUCE-free device contract. `MagdaDevice` (`magda/daw/audio/plugins/MagdaDevice.hpp`) adds how the app describes parameters.

| Call | Thread |
|------|--------|
| `prepare()`, `release()`, `reset()` | message thread, before/after rendering |
| `parameterValue()`, `restoreState()`, `latencySamples()`, `tailSamples()` | message thread |
| `process(ProcessContext&)`, `setParameterValue()` | audio thread, every block |

- Never touch a `ValueTree`, `magda::UndoManager` or model manager from `process()`.
- Everything a device needs per block arrives in `sdk::ProcessContext`.

### ProcessContext Quick Reference

```cpp
void process(DeviceProcessContext& ctx) override {   // DeviceProcessContext = sdk::ProcessContext
    auto& audio = ctx.audio;               // sdk::BufferView: host channel pointers, frame 0 is the block's first frame
    int n = ctx.numSamples();
    const auto* midiIn = ctx.midiIn;       // sdk::MidiInput*, may be null: no MIDI routed
    auto* midiOut = ctx.midiOut;           // sdk::MidiOutput*, null with midiIn; addEvent returns false once the port's budget is spent
    const auto* tempo = ctx.tempoMap;
    bool playing = ctx.isPlaying;
    // ctx.sidechain: std::optional<ConstBufferView>, absent when nothing is routed
    // sample rate and max block size come from prepare(), not from the context
    // juceAudio(ctx) and DeviceMidiInput/DeviceMidiOutput (DeviceJuceInterop.hpp) are the JUCE views
}
```

### Compiled Effects

Compiled Faust effects derive from `devices::faust::CompiledEffect` (`magda/devices/faust/`, JUCE-free). `process()` is final in shape: it writes zones, then calls `beforeCompute()`, `processAudio()`, `afterCompute()`. A device with custom DSP overrides `processAudio(sdk::ProcessContext&)`; the same audio-thread rules apply to every hook.

## Common Patterns in This Codebase

### Per-Chain Peak Metering

```cpp
// In a multi-chain device (e.g., drum grid with multiple output chains):
struct Chain {
    std::atomic<float> peak{0.0f};
    // ... other chain state ...
};

std::array<Chain, 16> chains;

// Audio thread: update peak for each chain
void process(DeviceProcessContext& ctx) override {
    for (int i = 0; i < numActiveChains; ++i) {
        float mag = getChainMagnitude(i, ctx);
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
void process(DeviceProcessContext& ctx) override {
    for (int i = 0; i < 16; ++i) {
        if (padTriggers[i].exchange(false, std::memory_order_relaxed)) {
            startPadPlayback(i);
        }
    }
}
```

## Debugging Checklist

If you hear clicks, dropouts, or glitches:

1. **Search for allocations** in `process()` - look for `new`, `String`, `Array`, `vector` operations
2. **Search for locks** - grep for `ScopedLock`, `lock_guard`, `CriticalSection` in audio path
3. **Check for DBG()** calls in audio code - these do file I/O
4. **Verify pre-allocation** - all buffers sized in `prepare()`, not in `process()`
5. **Check for ValueTree or CachedValue reads** on the audio path - parameters arrive resolved, state via `restoreState()`
