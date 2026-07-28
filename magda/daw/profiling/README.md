# MAGDA Performance Profiling System

Comprehensive performance profiling and benchmarking infrastructure for periodic checks.

## Overview

The profiling system provides tools for measuring and tracking performance across all critical areas:

1. **Audio Thread Performance** - Buffer processing times, CPU usage, overruns
2. **UI Responsiveness** - Frame times, dropped frames
3. **Plugin Performance** - Scan times, load times, failures
4. **Memory Usage** - Current and peak memory consumption
5. **MIDI Latency** - Input to processing latency

## Quick Start

### 1. Add Profiling to Critical Code Paths

```cpp
#include "profiling/PerformanceProfiler.hpp"

// In audio callback (DeviceProcessor)
void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    MAGDA_MONITOR_SCOPE("AudioCallback");

    // Your audio processing code...
}

// In UI rendering
void paint(juce::Graphics& g) override {
    MAGDA_MONITOR_SCOPE("UIFrame");

    // Your rendering code...
}

// In plugin loading
te::Plugin::Ptr loadPlugin(const juce::PluginDescription& desc) {
    MAGDA_MONITOR_SCOPE("PluginLoad");

    // Plugin loading code...
}
```

### 2. Enable Periodic Benchmarking

```cpp
#include "profiling/BenchmarkSuite.hpp"

// In your application startup (MainWindow or similar):
auto benchmarkDir = juce::File::getSpecialLocation(
    juce::File::userApplicationDataDirectory)
    .getChildFile("MAGDA")
    .getChildFile("Benchmarks");

periodicRunner_ = std::make_unique<PeriodicBenchmarkRunner>(engine_, benchmarkDir);
periodicRunner_->start(30);  // Run benchmark every 30 minutes
```

### 3. View Results

Benchmark results are saved as CSV files with timestamps:
```
~/Library/Application Support/MAGDA/Benchmarks/
  benchmark_20260125_143022.csv
  benchmark_20260125_173022.csv
  ...
```

Each CSV contains:
- Timestamp
- Audio callback avg/max times
- CPU usage
- UI frame times
- Plugin performance
- Memory usage
- MIDI latency

## Manual Benchmarking

You can also run benchmarks on-demand:

```cpp
BenchmarkSuite suite;
suite.startContinuousMonitoring();

// ... run your workload ...

auto results = suite.stopContinuousMonitoring();
DBG(results.toFormattedString());

// Save to file
suite.saveBenchmarkResults(results, benchmarkFile);
```

## Profiling Macros

### MAGDA_PROFILE_SCOPE(name)
One-shot profiling that logs to console when scope exceeds 1ms:
```cpp
void expensiveOperation() {
    MAGDA_PROFILE_SCOPE("ExpensiveOp");
    // ... code ...
}  // Logs: [PROFILE] ExpensiveOp: 2.34 ms
```

### MAGDA_MONITOR_SCOPE(category)
Continuous monitoring that collects statistics:
```cpp
void audioCallback() {
    MAGDA_MONITOR_SCOPE("AudioCallback");
    // Stats are collected in PerformanceMonitor
}
```

### MAGDA_PROFILE_FUNCTION()
Convenience macro that uses `__FUNCTION__` as the name:
```cpp
void MyClass::processAudio() {
    MAGDA_PROFILE_FUNCTION();  // Uses "MyClass::processAudio"
}
```

## Performance Targets

### Audio Thread
- **Buffer callback time**: < 50% of buffer duration
  - At 512 samples @ 48kHz = 10.67ms buffer
  - Target: < 5ms avg, < 8ms max
- **CPU usage**: < 40% sustained
- **Overruns**: 0 per session

### UI Thread
- **Frame time**: < 16.67ms (60 FPS)
- **Dropped frames**: < 1% of total frames
- **Event latency**: < 50ms

### Plugin Loading
- **Scan time**: < 500ms per plugin
- **Load time**: < 200ms per plugin
- **Failure rate**: < 5%

### Memory
- **Baseline**: < 200 MB (no project)
- **Per track**: < 10 MB
- **Per plugin**: < 50 MB
- **Peak growth**: < 2x baseline

## Integration Points

### Audio Callback
In the concrete engine backend's audio callback:
```cpp
void audioCallback(const juce::AudioSourceChannelInfo& info) override {
    MAGDA_MONITOR_SCOPE("AudioCallback");
    // ... existing code ...
}
```

### UI Rendering
In `MainWindow::paint()` or panel paint methods:
```cpp
void paint(juce::Graphics& g) override {
    MAGDA_MONITOR_SCOPE("UIFrame");
    // ... existing code ...
}
```

### Plugin Operations
In `AudioBridge::loadExternalPlugin()`:
```cpp
PluginLoadResult loadExternalPlugin(TrackId trackId,
                                   const juce::PluginDescription& desc) {
    MAGDA_MONITOR_SCOPE("PluginLoad");
    // ... existing code ...
}
```

## Analyzing Results

### CSV Import
Import benchmark CSVs into Excel, Numbers, or Python/R for analysis:
```python
import pandas as pd
import matplotlib.pyplot as plt

df = pd.read_csv('benchmark_20260125_143022.csv')
df['Timestamp'] = pd.to_datetime(df['Timestamp'])

# Plot audio callback times over time
plt.plot(df['Timestamp'], df['AudioCallbackAvg'])
plt.xlabel('Time')
plt.ylabel('Avg Audio Callback (ms)')
plt.show()
```

### Performance Regression Detection
Compare benchmarks over time to detect regressions:
```bash
# Compare before/after a change
diff benchmark_before.csv benchmark_after.csv
```

## Build Configuration

Profiling is enabled in Debug builds only (controlled by `JUCE_DEBUG`).

In Release builds, macros compile to no-ops for zero overhead.

## Troubleshooting

### High Audio Callback Times
- Check plugin count (each adds overhead)
- Profile individual plugins with MAGDA_PROFILE_SCOPE
- Verify buffer size settings
- Check for blocking operations in audio thread

### High Memory Usage
- Check for memory leaks (use Instruments on macOS)
- Verify plugin cleanup on removal
- Check clip pool size

### Dropped UI Frames
- Reduce metering update frequency
- Optimize paint() methods
- Check for blocking UI thread operations
- Profile with Xcode Instruments

## Future Enhancements

- [ ] Real-time dashboard panel in UI
- [ ] Automatic regression detection
- [ ] Performance comparison reports
- [ ] Integration with CI/CD for automated benchmarking
- [ ] Per-plugin performance breakdown
- [ ] Network monitoring for remote collaboration features
