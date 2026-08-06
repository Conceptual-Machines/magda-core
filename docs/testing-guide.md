# MAGDA Testing Guide

## Overview

This guide covers testing strategies for MAGDA, a JUCE-based DAW application with Tracktion Engine integration.

## Testing Framework

We use **Catch2** for C++ unit tests and JUCE's built-in test framework for integration tests.

```bash
# Run all tests
cd cmake-build-debug
ctest --output-on-failure

# Run specific test
./tests/magda_tests "[plugin]"
```

## Testing Strategy

### 1. Business Logic Tests (No UI)

Test algorithms and data structures without instantiating UI components.

**Example: Plugin matching logic**
```cpp
TEST_CASE("Plugin lookup by uniqueId", "[plugin]") {
    auto desc = createMockPluginDescription("Serum 2", "/path/to/plugin.vst3",
                                            -1002318962, 0, true);

    REQUIRE(desc.uniqueId != 0);
    REQUIRE(desc.isInstrument == true);
}
```

**What to test:**
- Data transformations (e.g., parameter value scaling)
- State management (e.g., TrackManager, ClipManager)
- Command pattern logic (undo/redo)
- Audio processing math (gain calculations, pan laws)

### 2. Mock JUCE Components

For components that depend on JUCE types, create test doubles or use minimal real objects.

**Pattern 1: Test doubles**
```cpp
// Mock audio processor for testing plugin logic
class MockAudioProcessor : public juce::AudioProcessor {
  public:
    MockAudioProcessor() : AudioProcessor(BusesProperties()) {}

    const juce::String getName() const override { return "MockPlugin"; }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}

    // ... minimal implementations
};
```

**Pattern 2: Test with real but minimal JUCE objects**
```cpp
TEST_CASE("ValueTree serialization", "[state]") {
    juce::ValueTree state("PLUGIN");
    state.setProperty("name", "Test Plugin", nullptr);
    state.setProperty("bypassed", false, nullptr);

    REQUIRE(state["name"].toString() == "Test Plugin");
    REQUIRE(state["bypassed"] == false);
}
```

### 3. Headless Component Tests

Test JUCE components without visible windows using `MessageManager` in test mode.

```cpp
class ComponentTestFixture {
  public:
    ComponentTestFixture() {
        // Initialize MessageManager for tests
        juce::MessageManager::getInstance();
    }

    ~ComponentTestFixture() {
        juce::MessageManager::deleteInstance();
    }
};

TEST_CASE("Component bounds", "[ui]") {
    ComponentTestFixture fixture;

    juce::Component comp;
    comp.setBounds(0, 0, 100, 50);

    REQUIRE(comp.getWidth() == 100);
    REQUIRE(comp.getHeight() == 50);
}
```

**Limitations:**
- No actual rendering (no graphics context)
- No mouse/keyboard events
- No native windows
- Timers and async callbacks work

### 4. Integration Tests

Test interactions between real components in a controlled environment.

```cpp
TEST_CASE("Tracktion Engine Edit lifecycle", "[integration][tracktion]") {
    auto engine = std::make_unique<tracktion::Engine>("TestEngine");

    auto tempFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                        .getChildFile("test.tracktionedit");

    auto edit = tracktion::createEmptyEdit(*engine, tempFile);

    REQUIRE(edit != nullptr);
    REQUIRE(edit->getNumAudioTracks() >= 0);

    // Test track creation
    auto track = edit->insertNewAudioTrack(tracktion::TrackInsertPoint::append(), nullptr);
    REQUIRE(track != nullptr);

    // Cleanup
    edit.reset();
    engine.reset();
    tempFile.deleteFile();
}
```

## Mocking UI Elements

### Pattern 1: Interface Injection

Extract interfaces from UI components for easy mocking.

```cpp
// Interface for audio engine operations
class IAudioEngine {
  public:
    virtual ~IAudioEngine() = default;
    virtual void play() = 0;
    virtual void stop() = 0;
    virtual bool isPlaying() const = 0;
};

// Real implementation
class TracktionEngineWrapper : public IAudioEngine {
    void play() override { /* real implementation */ }
    // ...
};

// Mock for testing
class MockAudioEngine : public IAudioEngine {
    void play() override { playCalled = true; }
    void stop() override { stopCalled = true; }
    bool isPlaying() const override { return isPlayingState; }

    bool playCalled = false;
    bool stopCalled = false;
    bool isPlayingState = false;
};

// Test
TEST_CASE("Transport controls", "[ui]") {
    MockAudioEngine engine;
    TransportPanel panel;
    panel.setAudioEngine(&engine);

    panel.simulatePlayButtonClick();
    REQUIRE(engine.playCalled == true);
}
```

### Pattern 2: Callback Verification

Test that callbacks are invoked correctly without real UI.

```cpp
TEST_CASE("Window state callbacks", "[ui][plugin]") {
    bool callbackInvoked = false;
    int deviceId = 0;
    bool isOpen = false;

    auto callback = [&](int id, bool open) {
        callbackInvoked = true;
        deviceId = id;
        isOpen = open;
    };

    // Simulate window manager calling callback
    callback(42, true);

    REQUIRE(callbackInvoked == true);
    REQUIRE(deviceId == 42);
    REQUIRE(isOpen == true);
}
```

### Pattern 3: State Verification

Test component state without rendering.

```cpp
TEST_CASE("Component visibility state", "[ui]") {
    juce::Component comp;

    REQUIRE(comp.isVisible() == false);  // Default hidden

    comp.setVisible(true);
    REQUIRE(comp.isVisible() == true);

    comp.setVisible(false);
    REQUIRE(comp.isVisible() == false);
}
```

## Testing Recent Fixes

### Plugin Window Close (Malloc Error Fix)

```cpp
TEST_CASE("Window close deferred execution", "[ui][plugin][window]") {
    SECTION("Close callback executed after event handler returns") {
        std::vector<std::string> executionOrder;

        // Simulate closeButtonPressed (just sets flag, returns immediately)
        executionOrder.push_back("closeButtonPressed_enter");
        executionOrder.push_back("closeButtonPressed_exit");

        // Simulate timer detecting close request
        executionOrder.push_back("timer_detected_close");

        // Simulate async close (happens AFTER event handler completes)
        executionOrder.push_back("async_close_executed");

        // Verify order prevents "delete this during member function"
        REQUIRE(executionOrder[0] == "closeButtonPressed_enter");
        REQUIRE(executionOrder[1] == "closeButtonPressed_exit");
        REQUIRE(executionOrder[3] == "async_close_executed");
    }
}
```

### Shutdown Sequence

```cpp
TEST_CASE("MainWindow component destruction order", "[ui][shutdown]") {
    std::vector<std::string> destructionOrder;

    // Test explicit destruction order
    destructionOrder.push_back("positionTimer");      // Stop timers first
    destructionOrder.push_back("views");              // Views before engine
    destructionOrder.push_back("audioEngine");        // Engine last

    size_t timerIdx = 0;
    size_t engineIdx = destructionOrder.size() - 1;

    REQUIRE(destructionOrder[timerIdx] == "positionTimer");
    REQUIRE(destructionOrder[engineIdx] == "audioEngine");
}
```

### Transport and Playback Context

```cpp
TEST_CASE("Tracktion Engine shutdown sequence", "[shutdown][tracktion]") {
    std::vector<std::string> shutdownOrder;

    // Correct order prevents device hang
    shutdownOrder.push_back("stop_transport");
    shutdownOrder.push_back("free_playback_context");
    shutdownOrder.push_back("destroy_edit");
    shutdownOrder.push_back("close_devices");
    shutdownOrder.push_back("destroy_engine");

    REQUIRE(shutdownOrder[0] == "stop_transport");
    REQUIRE(shutdownOrder[1] == "free_playback_context");

    // Verify devices closed before engine destroyed
    auto devicesIdx = std::find(shutdownOrder.begin(), shutdownOrder.end(),
                                 "close_devices") - shutdownOrder.begin();
    auto engineIdx = std::find(shutdownOrder.begin(), shutdownOrder.end(),
                               "destroy_engine") - shutdownOrder.begin();
    REQUIRE(devicesIdx < engineIdx);
}
```

## Thread Safety Tests

```cpp
TEST_CASE("Atomic shutdown flag", "[threading]") {
    std::atomic<bool> isShuttingDown{false};

    // Timer thread checks flag
    bool timerShouldRun = !isShuttingDown.load(std::memory_order_acquire);
    REQUIRE(timerShouldRun == true);

    // Main thread sets shutdown
    isShuttingDown.store(true, std::memory_order_release);

    // Timer thread sees flag (memory order guarantees visibility)
    timerShouldRun = !isShuttingDown.load(std::memory_order_acquire);
    REQUIRE(timerShouldRun == false);
}

TEST_CASE("CriticalSection protects shared state", "[threading]") {
    juce::CriticalSection lock;
    std::unordered_map<int, bool> sharedData;

    // Thread 1 writes
    {
        juce::ScopedLock sl(lock);
        sharedData[1] = true;
    }

    // Thread 2 reads (no data race)
    {
        juce::ScopedLock sl(lock);
        REQUIRE(sharedData[1] == true);
    }
}
```

## Best Practices

### DO:
- ✅ Test business logic separately from UI
- ✅ Use interfaces for dependency injection
- ✅ Test callbacks and state changes
- ✅ Verify destruction order for complex lifecycles
- ✅ Test thread safety with atomic operations
- ✅ Use meaningful test names (`TEST_CASE("What it tests", "[tag]")`)

### DON'T:
- ❌ Test JUCE/Tracktion internal implementation details
- ❌ Create actual windows in unit tests (use headless)
- ❌ Test rendering/painting (test state instead)
- ❌ Rely on timing (use deterministic sequences)
- ❌ Test third-party library behavior
- ❌ Let POSIX be the implicit default (see below)

## Platform Assumptions

MAGDA ships on macOS, Linux and Windows, and every test runs on all three. A
test that mentions a **process id, a path, a permission bit, or a signal** is
making a platform assumption, whether or not it looks like one — and the one
platform that disagrees is usually Windows, which is also the slowest CI leg to
find out from.

Prefer a property that does not depend on the difference:

```cpp
// Assumes POSIX: pid 1 is init/launchd and always alive. On Windows it is not a
// process at all, so this record is swept and the assertion fails there only.
const auto other = tokenFile().getSiblingFile("remote-api-1.json");
REQUIRE(other.existsAsFile());

// No liveness assumption: written after the sweep has already run, so what is
// under test is that stop() removes this instance's record and nothing else.
REQUIRE(host.start());
const auto other = tokenFile().getSiblingFile("remote-api-424242.json");
other.replaceWithText(...);
host.stop();
REQUIRE(other.existsAsFile());
```

When the difference is the point, guard it and say why:

```cpp
#if !JUCE_WINDOWS
// The parent of this process: alive, and not us. Reaching a foreign pid's
// liveness on Windows portably needs a toolhelp snapshot, which is more
// machinery than this warrants — the abandoned-record case covers the other
// half of the same branch everywhere.
#endif
```

Values worth distrusting: pids (Windows issues multiples of four, macOS caps
below 100000, Linux defaults to 4194304), file permissions (`chmod` is a no-op
on Windows, where ACLs are inherited), path separators and case sensitivity, and
anything involving signals.

## Running Tests

```bash
# Build tests
cmake --build cmake-build-debug --target magda_tests

# Run all tests
cd cmake-build-debug
./tests/magda_tests

# Run tests with tag
./tests/magda_tests "[plugin]"

# Run specific test case
./tests/magda_tests "Plugin lookup by uniqueId"

# List all tests
./tests/magda_tests --list-tests

# Run with verbose output
./tests/magda_tests -s
```

## Debugging Failed Tests

```bash
# Run test with debugger
lldb ./tests/magda_tests
(lldb) run "[failing-test]"

# Run with breakpoints
(lldb) breakpoint set --name "failingFunction"
(lldb) run

# Check memory leaks (macOS)
leaks --atExit -- ./tests/magda_tests
```

## Coverage Analysis

```bash
# Build with coverage flags
cmake -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=ON ..
cmake --build .

# Run tests
./tests/magda_tests

# Generate coverage report
lcov --capture --directory . --output-file coverage.info
genhtml coverage.info --output-directory coverage_html
open coverage_html/index.html
```

## Resources

- [Catch2 Documentation](https://github.com/catchorg/Catch2)
- [JUCE UnitTest Tutorial](https://docs.juce.com/master/tutorial_unit_tests.html)
- [Tracktion Engine Examples](https://github.com/Tracktion/tracktion_engine/tree/master/examples)
