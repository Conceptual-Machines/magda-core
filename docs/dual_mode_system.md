# 🎛️ Dual-Mode System Design

## Overview

Magica implements a sophisticated dual-mode system inspired by Ableton Live, providing both **Performance** and **Arrangement** views, along with **Live** and **Studio** audio modes for optimal real-time and production workflows.

## 🎯 Dual-Mode Architecture

### 1. View Modes (Performance vs Arrangement)

#### **Arrangement Mode** (Traditional DAW View)
```
┌─────────────────────────────────────────────────────────┐
│ Transport Controls                                      │
├─────────────────────────────────────────────────────────┤
│ Timeline View                                           │
│ ┌─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┐ │
│ │Track│ 1   │ 2   │ 3   │ 4   │ 5   │ 6   │ 7   │ 8   │ │
│ ├─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┤ │
│ │Drums│ ████│     │ ████│     │ ████│     │ ████│     │ │
│ ├─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┤ │
│ │Bass │     │ ████│     │ ████│     │ ████│     │ ████│ │
│ ├─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┤ │
│ │Lead │ ████│     │ ████│     │ ████│     │ ████│     │ │
│ └─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┘ │
├─────────────────────────────────────────────────────────┤
│ Mixer Panel                                             │
│ ┌─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┐ │
│ │Vol  │ 0.8 │ 0.7 │ 0.9 │     │     │     │     │ 1.0 │ │
│ │Pan  │ 0.0 │-0.2 │ 0.1 │     │     │     │     │ 0.0 │ │
│ └─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┘ │
└─────────────────────────────────────────────────────────┘
```

**Features:**
- Traditional timeline with tracks and clips
- Linear arrangement view
- Track mixer with volume/pan controls
- Transport controls (play, stop, record)
- Timeline navigation and editing

#### **Performance Mode** (Ableton Live Session View)
```
┌─────────────────────────────────────────────────────────┐
│ Mode Switcher: [Arrangement] [Performance] [Live] [Studio] │
├─────────────────────────────────────────────────────────┤
│ Performance View - Clip Launcher                         │
│ ┌─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┐ │
│ │Track│Scene│Scene│Scene│Scene│Scene│Scene│Scene│Scene│ │
│ │     │  1  │  2  │  3  │  4  │  5  │  6  │  7  │  8  │ │
│ ├─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┤ │
│ │Drums│ [▶] │ [▶] │ [▶] │ [▶] │ [▶] │ [▶] │ [▶] │ [▶] │ │
│ ├─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┤ │
│ │Bass │ [▶] │ [▶] │ [▶] │ [▶] │ [▶] │ [▶] │ [▶] │ [▶] │ │
│ ├─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┤ │
│ │Lead │ [▶] │ [▶] │ [▶] │ [▶] │ [▶] │ [▶] │ [▶] │ [▶] │ │
│ ├─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┤ │
│ │Scene│ [▶] │ [▶] │ [▶] │ [▶] │ [▶] │ [▶] │ [▶] │ [▶] │ │
│ └─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┘ │
├─────────────────────────────────────────────────────────┤
│ Real-time Stats: Latency: 5.8ms | CPU: 23% | Buffer: 256 │
└─────────────────────────────────────────────────────────┘
```

**Features:**
- Grid-based clip launcher
- Real-time clip triggering
- Scene launching (multiple clips at once)
- Quantized launching (next beat, next bar, etc.)
- Visual feedback for playing clips
- Real-time performance metrics

### 2. Audio Modes (Live vs Studio)

#### **Live Mode** (Real-time Performance)
```
Audio Configuration:
├── Buffer Size: 256 samples
├── Sample Rate: 44.1 kHz
├── Latency: ~5.8ms
├── CPU Priority: Real-time
├── Plugin Processing: Minimal
└── Focus: Low latency for live performance
```

**Use Cases:**
- Live performance with AI agents
- Real-time jam sessions
- Live recording
- Interactive music creation
- Low-latency monitoring

#### **Studio Mode** (Production Quality)
```
Audio Configuration:
├── Buffer Size: 1024 samples
├── Sample Rate: 48 kHz
├── Latency: ~21.3ms
├── CPU Priority: Quality
├── Plugin Processing: Full
└── Focus: High quality for production
```

**Use Cases:**
- Studio recording and mixing
- High-quality rendering
- Complex plugin chains
- Offline processing
- Final production work

## 🔧 Technical Implementation

### 1. Mode Switching Architecture

```cpp
// Mode switching flow
Agent Request → gRPC → DAWModeInterface → TracktionEngine → Audio Engine
     ↓              ↓           ↓              ↓              ↓
"Set Live Mode" → SetAudioMode → applyAudioConfig → updateBufferSize → restartAudio
```

### 2. Real-time Mode Switching

```cpp
class DAWModeInterfaceImpl {
private:
    ViewMode currentViewMode_ = ViewMode::Arrangement;
    AudioMode currentAudioMode_ = AudioMode::Studio;
    
    // Audio configurations
    AudioConfig liveConfig_ = {256, 44100, 5.8};   // Low latency
    AudioConfig studioConfig_ = {1024, 48000, 21.3}; // High quality
    
public:
    void setAudioMode(AudioMode mode) override {
        currentAudioMode_ = mode;
        auto& config = (mode == AudioMode::Live) ? liveConfig_ : studioConfig_;
        applyAudioConfiguration(config);
        notifyAudioModeChanged();
    }
};
```

### 3. Performance Clip Launching

```cpp
void DAWModeInterfaceImpl::launchClip(const std::string& clip_id, double quantize_beats) {
    auto* clip = findClipById(clip_id);
    if (clip) {
        scheduleClipLaunch(clip, quantize_beats);
        playingClips_.push_back(clip_id);
    }
}
```

## 🎵 Use Cases and Workflows

### 1. Live Performance with AI Agents

```
Workflow:
1. Switch to Performance Mode + Live Audio Mode
2. AI agent analyzes current musical context
3. Agent suggests and launches appropriate clips
4. Real-time collaboration between human and AI
5. Low latency ensures responsive performance
```

### 2. Studio Production

```
Workflow:
1. Switch to Arrangement Mode + Studio Audio Mode
2. Traditional DAW workflow for arrangement
3. High-quality audio processing
4. Complex plugin chains
5. Final mixdown and export
```

### 3. Hybrid Workflow

```
Workflow:
1. Start in Performance Mode for idea generation
2. Switch to Arrangement Mode for detailed editing
3. Toggle between Live/Studio modes as needed
4. Seamless transition between workflows
```

## 🤖 AI Agent Integration

### 1. Real-time Jam Assistant (Live Mode)

```python
# AI agent in live mode
async def live_jam_assistant():
    while True:
        # Analyze current musical context
        context = await analyze_current_music()
        
        # Suggest next clip based on context
        suggested_clip = await suggest_next_clip(context)
        
        # Launch clip with quantization
        await daw.launchClip(suggested_clip, quantize_beats=1.0)
        
        await asyncio.sleep(0.1)  # Real-time response
```

### 2. Production Assistant (Studio Mode)

```python
# AI agent in studio mode
async def production_assistant():
    # Switch to studio mode for quality
    await daw.setAudioMode(AudioMode.STUDIO)
    
    # High-quality processing
    await daw.addEffect("reverb", {"room_size": 0.8})
    await daw.addEffect("compressor", {"threshold": -20})
    
    # Detailed arrangement work
    await daw.createMidiClip("verse_melody", start_time=16.0, length=8.0)
```

## 📊 Performance Metrics

### Real-time Monitoring

```cpp
struct AudioStats {
    int bufferSize;
    int sampleRate;
    double latencyMs;
    double cpuUsage;
    int activePlugins;
    double memoryUsage;
};
```

### Mode-specific Optimizations

#### Live Mode Optimizations:
- Minimal plugin processing
- Real-time priority threads
- Reduced buffer sizes
- Optimized audio routing
- Quick mode switching

#### Studio Mode Optimizations:
- Full plugin processing
- Quality-focused algorithms
- Larger buffer sizes
- Complex audio routing
- High-quality rendering

## 🔄 Mode Transition Handling

### 1. Smooth Transitions

```cpp
void DAWModeInterfaceImpl::setViewMode(ViewMode mode) {
    if (mode != currentViewMode_) {
        // Save current state
        saveCurrentState();
        
        // Switch mode
        currentViewMode_ = mode;
        
        // Restore appropriate state
        restoreModeState(mode);
        
        // Notify UI
        notifyViewModeChanged();
    }
}
```

### 2. State Persistence

```cpp
struct ModeState {
    ViewMode viewMode;
    AudioMode audioMode;
    std::vector<std::string> playingClips;
    double currentPosition;
    double tempo;
    // ... other state
};
```

## 🎯 Benefits of Dual-Mode System

### 1. **Workflow Flexibility**
- Seamless switching between performance and production
- Optimized for different use cases
- Familiar interfaces for different user types

### 2. **AI Agent Optimization**
- Real-time agents can work in Live mode
- Production agents can work in Studio mode
- Mode-specific AI behaviors

### 3. **Performance Optimization**
- Live mode for low-latency performance
- Studio mode for high-quality production
- Automatic optimization based on mode

### 4. **User Experience**
- Intuitive mode switching
- Visual feedback for current mode
- Real-time performance metrics
- Familiar Ableton Live workflow

This dual-mode system makes Magica uniquely suited for both live performance with AI agents and traditional studio production, providing the best of both worlds in a single, integrated DAW system. 