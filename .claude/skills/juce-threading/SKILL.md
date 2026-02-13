---
name: juce-threading
description: JUCE threading model and component lifecycle safety. Use when writing UI components, async callbacks, timers, or any code that crosses thread boundaries. Covers destruction ordering, SafePointer, LookAndFeel cleanup, and common crash patterns.
---

# JUCE Threading Model & Component Lifecycle

This skill covers thread safety and component lifecycle patterns critical for avoiding crashes in MAGDA.

## Threading Model

### Three Thread Contexts

```
MessageThread (UI)     — Component::paint, resized, mouse events, Timer callbacks, callAsync lambdas
Audio Thread (RT)      — processBlock, no allocations, no locks, no MessageManager calls
Background Threads     — juce::Thread, juce::ThreadPool, long-running tasks
```

### Check Which Thread You're On

```cpp
// Assert you're on the message thread
jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());

// Check without asserting
if (juce::MessageManager::getInstance()->isThisTheMessageThread())
    component.repaint();
```

### Cross-Thread Communication

```cpp
// Audio thread → UI thread (fire-and-forget)
juce::MessageManager::callAsync([this] {
    // Runs on message thread. WARNING: `this` may be dead — see SafePointer below.
    label.setText("Done", juce::dontSendNotification);
});

// Audio thread → UI thread (coalesced, no allocation)
struct MyComp : juce::Component, juce::AsyncUpdater {
    void audioCallback() {
        latestValue.store(newVal);
        triggerAsyncUpdate();  // Safe from any thread, coalesces multiple calls
    }
    void handleAsyncUpdate() override {
        // Runs on message thread, once per coalesced batch
        slider.setValue(latestValue.load());
    }
    std::atomic<float> latestValue { 0.0f };
};

// Periodic polling from UI thread
struct MyComp : juce::Component, juce::Timer {
    MyComp() { startTimerHz(30); }
    ~MyComp() override { stopTimer(); }  // MUST stop in destructor
    void timerCallback() override {
        // Runs on message thread
        repaint();
    }
};

// Delayed one-shot on message thread
juce::MessageManager::callAsync([] {
    juce::Timer::callAfterDelay(500, [] {
        // Runs on message thread after 500ms
    });
});
```

### Audio Thread Rules

```cpp
void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
{
    // NEVER do any of these on the audio thread:
    // - new / delete / malloc
    // - juce::String construction or concatenation
    // - MessageManager::callAsync (allocates — use AsyncUpdater instead)
    // - Lock acquisition (mutex, CriticalSection)
    // - File I/O
    // - DBG() in release builds

    // SAFE on audio thread:
    // - std::atomic reads/writes
    // - Lock-free queues (juce::AbstractFifo)
    // - Raw pointer/buffer arithmetic
    // - triggerAsyncUpdate() (lock-free)
}
```

## Component Lifecycle & Destruction Safety

### Destruction Ordering

```cpp
struct ParentComp : juce::Component {
    ~ParentComp() override
    {
        stopTimer();            // 1. Stop all timers
        removeListener(this);   // 2. Remove all listener registrations
        // 3. Children are destroyed automatically after this (member order, reverse)
    }

    ChildComp child;  // Destroyed BEFORE ParentComp destructor body runs?
                      // NO — members are destroyed AFTER destructor body.
                      // So stopTimer() in destructor body is safe.
};
```

**Rule**: Stop timers and remove listeners in the destructor body, before members are destroyed.

### SafePointer for Async Safety

```cpp
// DANGEROUS — `this` may be deleted before lambda runs
juce::MessageManager::callAsync([this] {
    setText("done");  // CRASH if component was deleted
});

// SAFE — SafePointer becomes nullptr if component is deleted
juce::MessageManager::callAsync([safeThis = juce::Component::SafePointer<MyComp>(this)] {
    if (auto* self = safeThis.getComponent())
        self->setText("done");
});

// SAFE — shorter form
auto safeThis = juce::Component::SafePointer<MyComp>(this);
juce::MessageManager::callAsync([safeThis]() mutable {
    if (safeThis != nullptr)
        safeThis->setText("done");
});
```

### Timer Safety

```cpp
struct MyComp : juce::Component, juce::Timer {
    MyComp() { startTimerHz(30); }

    ~MyComp() override {
        stopTimer();  // MANDATORY — timer callback must not fire after destruction
    }

    void timerCallback() override {
        // Safe to access members here — guaranteed on message thread,
        // and stopTimer() in destructor prevents post-destruction calls
        repaint();
    }
};
```

### LookAndFeel Cleanup

```cpp
// DANGEROUS — components reference L&F that's already destroyed
struct MyApp {
    CustomLookAndFeel lnf;       // Destroyed SECOND (after mainWindow)
    std::unique_ptr<MainWindow> mainWindow;  // Destroyed FIRST — but its children
                                              // may still reference lnf during teardown
};

// SAFE — clear default L&F before destroying components
struct MyApp {
    ~MyApp() {
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);  // Clear global L&F
        mainWindow.reset();  // Now safe to destroy components
    }
    CustomLookAndFeel lnf;
    std::unique_ptr<MainWindow> mainWindow;
};

// SAFE — per-component L&F cleanup
struct MyComp : juce::Component {
    ~MyComp() override {
        setLookAndFeel(nullptr);  // Detach before L&F may be destroyed
    }
};
```

### Listener Removal

```cpp
struct MyComp : juce::Component, juce::Value::Listener {
    MyComp(juce::Value& v) : value(v) {
        value.addListener(this);
    }

    ~MyComp() override {
        value.removeListener(this);  // MUST remove before destruction
    }

    void valueChanged(juce::Value&) override {
        repaint();
    }

    juce::Value value;
};
```

### JUCE Leak Detector

```cpp
class MyComponent : public juce::Component {
public:
    MyComponent();
    ~MyComponent() override;

private:
    // Place at the END of the class — detects leaked instances on shutdown
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MyComponent)
};
```

## Common Crash Patterns & Fixes

### 1. Timer fires after component deleted

```cpp
// BUG: no stopTimer() in destructor
struct Bad : juce::Component, juce::Timer {
    ~Bad() override { /* timer still running! */ }
};

// FIX:
struct Good : juce::Component, juce::Timer {
    ~Good() override { stopTimer(); }
};
```

### 2. callAsync lambda captures dead `this`

```cpp
// BUG:
juce::MessageManager::callAsync([this] { repaint(); });

// FIX:
juce::MessageManager::callAsync(
    [safe = juce::Component::SafePointer<MyComp>(this)] {
        if (safe != nullptr) safe->repaint();
    });
```

### 3. LookAndFeel destroyed before components

```cpp
// BUG: member destruction order destroys L&F while components still reference it
struct App {
    CustomLookAndFeel lnf;
    std::unique_ptr<MainWindow> window;  // window's children still use lnf during destruction
};

// FIX: explicit teardown order
struct App {
    ~App() {
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
        window.reset();
    }
    CustomLookAndFeel lnf;
    std::unique_ptr<MainWindow> window;
};
```

### 4. Listener not removed before object destroyed

```cpp
// BUG: broadcaster calls dead listener
struct Bad : juce::Button::Listener {
    Bad(juce::Button& b) : button(b) { button.addListener(this); }
    ~Bad() { /* forgot removeListener! */ }
    juce::Button& button;
};

// FIX:
struct Good : juce::Button::Listener {
    Good(juce::Button& b) : button(b) { button.addListener(this); }
    ~Good() { button.removeListener(this); }
    juce::Button& button;
};
```

### 5. Component deleted during its own callback

```cpp
// BUG: deleting yourself inside a button callback
void buttonClicked(juce::Button*) override {
    owner.removeChildComponent(this);
    delete this;  // CRASH — stack still unwinding through JUCE event dispatch
}

// FIX: defer deletion
void buttonClicked(juce::Button*) override {
    juce::MessageManager::callAsync([this, &owner = this->owner] {
        owner.removeAndDeleteChild(this);
    });
}
```

### 6. Accessing MessageManager from audio thread

```cpp
// BUG: called from processBlock
void processBlock(...) {
    if (clipping)
        component.repaint();  // WRONG — repaint() posts to message thread internally,
                               // but is not safe from audio thread
}

// FIX: use atomic flag + timer or AsyncUpdater
void processBlock(...) {
    if (clipping)
        clipFlag.store(true);  // Lock-free
}

void timerCallback() override {
    if (clipFlag.exchange(false))
        clipIndicator.repaint();
}
```

## Prefer RAII Over Manual Resource Management

Always use RAII classes instead of manual acquire/release patterns. This prevents resource leaks when exceptions occur or early returns are taken.

### Smart Pointers Over Raw new/delete

```cpp
// BAD — leak if exception or early return between new and delete
auto* comp = new MyComponent();
addAndMakeVisible(comp);
// ... if something throws or returns, comp is leaked

// GOOD — ownership is automatic
auto comp = std::make_unique<MyComponent>();
addAndMakeVisible(comp.get());
ownedComponents.push_back(std::move(comp));
```

### Scoped Locks

```cpp
// BAD — manual lock/unlock, easy to forget unlock on early return
mutex.lock();
doWork();
if (error) return;  // BUG: mutex not unlocked!
mutex.unlock();

// GOOD — RAII lock, always released on scope exit
{
    const juce::ScopedLock sl(mutex);
    doWork();
    if (error) return;  // mutex automatically unlocked
}
```

### Tracktion Engine RAII Helpers

```cpp
// Prevent expensive playback graph rebuilds during batch operations
{
    te::TransportControl::ReallocationInhibitor inhibitor(transport);
    // ... make many changes to tracks/plugins ...
}  // Single graph rebuild happens here

// Restore playback state after temporary stop
{
    te::TransportControl::ScopedPlaybackRestarter restarter(transport);
    transport.stop(false, false);
    // ... do work that requires transport stopped ...
}  // Playback automatically resumes

// Group undo operations
{
    te::Edit::UndoTransactionInhibitor inhibitor(*edit);
    // ... multiple operations won't be grouped into one undo step ...
}
```

### RAII for Listener Registration

```cpp
// Pattern: register in constructor, unregister in destructor
struct ScopedListener {
    ScopedListener(juce::Value& v, juce::Value::Listener* l)
        : value(v), listener(l) { value.addListener(listener); }
    ~ScopedListener() { value.removeListener(listener); }
    juce::Value& value;
    juce::Value::Listener* listener;
};
```

### RAII for Timer Management

```cpp
// If you inherit from Timer, always stop in destructor:
~MyComp() override { stopTimer(); }

// Or use a helper that auto-stops:
struct ScopedTimer : juce::Timer {
    std::function<void()> callback;
    ~ScopedTimer() override { stopTimer(); }
    void timerCallback() override { if (callback) callback(); }
};
```

## Thread-Safe Data Patterns

```cpp
// Atomic for simple values
std::atomic<float> gain { 1.0f };
std::atomic<bool> shouldStop { false };

// Lock-free FIFO for messages (audio → UI)
juce::AbstractFifo fifo { 512 };
std::array<float, 512> buffer;

// SpinLock for very short critical sections (avoid on audio thread if possible)
juce::SpinLock lock;
{
    const juce::SpinLock::ScopedLockType sl(lock);
    // Very brief work only
}
```
