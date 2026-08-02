# Profiling Integration Guide

## Step-by-Step Integration into MAGDA

### 1. Add to CMakeLists.txt

Add the profiling directory to your build:

```cmake
# In magda/daw/CMakeLists.txt
target_sources(magda_daw PRIVATE
    # ... existing sources ...

    # Profiling (header-only)
    profiling/PerformanceProfiler.hpp
    profiling/BenchmarkSuite.hpp
)
```

### 2. Integrate Audio Thread Profiling

In the concrete engine backend's audio callback:

```cpp
#include "../profiling/PerformanceProfiler.hpp"

void ConcreteAudioEngine::audioCallback(
    const juce::AudioSourceChannelInfo& bufferToFill) {

    MAGDA_MONITOR_SCOPE("AudioCallback");

    // Existing audio processing code...
    audioBridge_->processParameterChanges();
    // ... etc
}
```

### 3. Integrate UI Thread Profiling

In `magda/daw/ui/windows/MainWindow.cpp`:

```cpp
#include "../../profiling/PerformanceProfiler.hpp"

class MainWindow : public juce::DocumentWindow {
    // ... existing code ...

    // Add periodic benchmark runner
    std::unique_ptr<magda::PeriodicBenchmarkRunner> benchmarkRunner_;

    void initializeBenchmarking() {
        auto benchmarkDir = juce::File::getSpecialLocation(
            juce::File::userApplicationDataDirectory)
            .getChildFile("MAGDA")
            .getChildFile("Benchmarks");

        benchmarkRunner_ = std::make_unique<magda::PeriodicBenchmarkRunner>(
            *wrapper_.getEngine(), benchmarkDir);

        // Run benchmark every 30 minutes
        benchmarkRunner_->start(30);
    }
};
```

Call `initializeBenchmarking()` in your `MainWindow` constructor.

### 4. Integrate Plugin Loading Profiling

In `magda/daw/audio/AudioBridge.cpp`:

```cpp
#include "../profiling/PerformanceProfiler.hpp"

PluginLoadResult AudioBridge::loadExternalPlugin(
    TrackId trackId,
    const juce::PluginDescription& description) {

    MAGDA_MONITOR_SCOPE("PluginLoad");

    // Existing plugin loading code...
}
```

### 5. Add Plugin Scanning Profiling

If you have a plugin scanner (e.g., `PluginScanner.cpp`):

```cpp
#include "../profiling/PerformanceProfiler.hpp"

void PluginScanner::scanPlugin(const juce::String& path) {
    MAGDA_MONITOR_SCOPE("PluginScan");

    // Existing scan code...
}
```

### 6. Monitor UI Panels

In mixer, piano roll, and other UI panels:

```cpp
void MixerView::paint(juce::Graphics& g) {
    MAGDA_MONITOR_SCOPE("UIFrame");

    // Existing paint code...
}
```

### 7. Add Menu Command for On-Demand Benchmark

In `magda/daw/ui/windows/MenuManager.cpp`:

```cpp
void MenuManager::createDebugMenu(juce::PopupMenu& menu) {
    // ... existing debug menu items ...

    menu.addItem("Run Performance Benchmark", [this]() {
        runBenchmark();
    });
}

void MenuManager::runBenchmark() {
    BenchmarkSuite suite;

    // Show progress...
    juce::AlertWindow::showMessageBoxAsync(
        juce::AlertWindow::InfoIcon,
        "Running Benchmark",
        "Collecting performance data for 10 seconds...",
        "OK");

    suite.startContinuousMonitoring();

    // Wait 10 seconds
    juce::Timer::callAfterDelay(10000, [suite]() mutable {
        auto results = suite.stopContinuousMonitoring();

        // Show results
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::InfoIcon,
            "Benchmark Results",
            results.toFormattedString(),
            "OK");

        // Save to file
        auto file = juce::File::getSpecialLocation(
            juce::File::userDocumentsDirectory)
            .getChildFile("magda_benchmark.csv");
        suite.saveBenchmarkResults(results, file);
    });
}
```

## Compilation Check

After integration, build to verify:

```bash
cd /path/to/magda-core
cmake --build build --target magda_daw
```

## Testing the Integration

1. **Run the application**
2. **Play back some audio** - check console for [PROFILE] messages
3. **Load some plugins** - verify profiling is working
4. **Check benchmark files**:
   ```bash
   ls ~/Library/Application\ Support/MAGDA/Benchmarks/
   ```

## Viewing Real-Time Stats

Add to MainWindow or create a debug panel:

```cpp
void showPerformanceStats() {
    auto& monitor = PerformanceMonitor::getInstance();
    auto allStats = monitor.getAllStats();

    juce::String report;
    for (const auto& [category, stats] : allStats) {
        report << category << ": " << stats.toString() << "\n";
    }

    DBG(report);
}
```

## Performance Budget Monitoring

Create alerts when performance exceeds targets:

```cpp
void checkPerformanceBudget() {
    auto& monitor = PerformanceMonitor::getInstance();

    auto audioStats = monitor.getStats("AudioCallback");
    if (audioStats.average() > 5.0) {  // 5ms budget
        DBG("WARNING: Audio callback exceeding budget: "
            << audioStats.average() << " ms");
    }

    auto uiStats = monitor.getStats("UIFrame");
    if (uiStats.average() > 16.67) {  // 60 FPS budget
        DBG("WARNING: UI frame time exceeding budget: "
            << uiStats.average() << " ms");
    }
}
```

## Conditional Profiling

Profile only specific scenarios:

```cpp
bool enableDetailedProfiling = false;  // Toggle via UI or settings

void processAudio() {
    if (enableDetailedProfiling) {
        MAGDA_MONITOR_SCOPE("AudioCallback");
    }
    // ... code ...
}
```

## Next Steps

After basic integration:
1. Run initial benchmarks to establish baseline
2. Set performance budgets per operation
3. Add CI/CD integration for automated regression detection
4. Create dashboard panel for real-time monitoring
5. Profile specific problem areas identified
