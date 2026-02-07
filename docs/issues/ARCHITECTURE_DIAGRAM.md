# AudioBridge Refactoring: Visual Architecture

## Current State (God Object)

```
┌─────────────────────────────────────────────────────────────┐
│                      AudioBridge                            │
│                    (3,592 lines)                            │
│                                                             │
│  • Track Mapping & Lifecycle                                │
│  • Clip Synchronization (Arrangement & Session)            │
│  • Plugin Loading & Management                              │
│  • Audio/MIDI Routing                                       │
│  • Metering (Track & Master)                                │
│  • Parameter Queue Management                               │
│  • Transport State                                          │
│  • MIDI Activity Monitoring                                 │
│  • Mixer Controls (Volume/Pan)                              │
│  • Warp Markers & Transient Detection                       │
│  • Plugin Window Management                                 │
│  • TrackManagerListener                                     │
│  • ClipManagerListener                                      │
│  • Timer Callbacks                                          │
│  • Thread Synchronization                                   │
│                                                             │
│  70+ methods, 60+ member variables, 19 responsibilities     │
└─────────────────────────────────────────────────────────────┘
```

**Problems:**
- Exceeds context windows for LLMs and humans
- Hard to test individual features
- Complex thread safety
- High coupling
- Changes ripple unpredictably

---

## Proposed State (Modular Architecture)

```
                     ┌──────────────────────┐
                     │   AudioBridge        │
                     │   (Coordinator)      │
                     │    ~500 LOC          │
                     └──────────┬───────────┘
                                │
                 ┌──────────────┼──────────────┐
                 │              │              │
     ┌───────────▼─────┐ ┌─────▼──────┐ ┌────▼──────────┐
     │  Mapping &      │ │  Audio     │ │  Routing      │
     │  Sync Modules   │ │  Processing│ │  Modules      │
     └───────┬─────────┘ └─────┬──────┘ └────┬──────────┘
             │                 │              │
    ┌────────┴────────┐   ┌────┴─────┐  ┌────┴─────┐
    │                 │   │          │  │          │
┌───▼───────────┐ ┌───▼──────────┐ ┌▼────────┐ ┌──▼────────┐
│TrackMapping   │ │MeteringMgr   │ │AudioRouting│ │MIDI       │
│Manager        │ │~300 LOC      │ │Manager     │ │Routing    │
│~300 LOC       │ └──────────────┘ │~200 LOC    │ │Manager    │
└───────────────┘                  └────────────┘ │~250 LOC   │
                                                  └───────────┘
┌───────────────┐ ┌──────────────┐
│PluginManager  │ │ParameterMgr  │
│~400 LOC       │ │~200 LOC      │
└───────────────┘ └──────────────┘
                                   ┌──────────────┐
┌───────────────┐ ┌──────────────┐│MixerCtrl     │
│ClipSync       │ │TransportState││~250 LOC      │
│~500 LOC       │ │Manager       │└──────────────┘
└───────────────┘ │~150 LOC      │
                  └──────────────┘ ┌──────────────┐
                                   │MIDIActivity  │
                  ┌──────────────┐ │Monitor       │
                  │WarpMarker    │ │~200 LOC      │
                  │Manager       │ └──────────────┘
                  │~300 LOC      │
                  └──────────────┘ ┌──────────────┐
                                   │PluginWindow  │
                                   │Bridge        │
                                   │~150 LOC      │
                                   └──────────────┘
```

---

## Module Organization

### 🎯 Core Coordination
- **AudioBridge** (500 LOC) - Thin facade, owns and coordinates modules

### 🗺️ Mapping & Synchronization (1,200 LOC total)
- **TrackMappingManager** (300 LOC) - Track ID ↔ TE AudioTrack
- **PluginManager** (400 LOC) - Device ID ↔ TE Plugin, loading
- **ClipSynchronizer** (500 LOC) - Clip lifecycle, arrangement & session

### 🎚️ Audio Processing (750 LOC total)
- **MeteringManager** (300 LOC) - Track & master metering
- **ParameterManager** (200 LOC) - Lock-free parameter queue
- **MixerController** (250 LOC) - Volume/pan controls

### 🔌 Routing (450 LOC total)
- **AudioRoutingManager** (200 LOC) - Audio I/O routing
- **MidiRoutingManager** (250 LOC) - MIDI device routing

### ⚡ Specialized Features (950 LOC total)
- **TransportStateManager** (150 LOC) - Playback state
- **MidiActivityMonitor** (200 LOC) - MIDI event tracking (fixes ID >= 128 bug)
- **WarpMarkerManager** (300 LOC) - Time-stretching markers
- **PluginWindowBridge** (150 LOC) - UI window delegation

**Total: ~3,850 LOC** (slightly more due to interfaces, but each module < 500 LOC)

---

## Implementation Phases

### 📦 Phase 1: Pure Data Managers (Low Risk)
Extract simple state holders with minimal TE interaction
- TransportStateManager
- MidiActivityMonitor  
- ParameterManager

**Risk Level:** ⚠️ Low

### 📦 Phase 2: Independent Features (Medium Risk)
Extract features with clear boundaries
- WarpMarkerManager
- PluginWindowBridge
- MixerController

**Risk Level:** ⚠️⚠️ Medium

### 📦 Phase 3: Core Mappers (Higher Risk)
Extract central mapping logic
- TrackMappingManager
- PluginManager
- ClipSynchronizer

**Risk Level:** ⚠️⚠️⚠️ Higher

### 📦 Phase 4: Routing & Metering (Highest Risk)
Extract complex TE-integrated systems
- AudioRoutingManager
- MidiRoutingManager
- MeteringManager

**Risk Level:** ⚠️⚠️⚠️⚠️ Highest

---

## Key Benefits

| Aspect | Before | After |
|--------|--------|-------|
| **Lines per file** | 3,592 | ~500 max |
| **Responsibilities** | 19 | 1 per module |
| **Testability** | Integration only | Unit + Integration |
| **Context fit** | ❌ Exceeds | ✅ Fits easily |
| **Thread safety** | Complex, spread | Clear per module |
| **Maintainability** | Difficult | Manageable |

---

## Thread Safety Model

Each module clearly documents its threading model:

- **UI Thread:** Mapping updates, property changes
- **Audio Thread:** Metering, parameter processing
- **Message Thread:** Timer callbacks, async operations

**Lock-Free Modules:**
- TransportStateManager (atomics)
- MidiActivityMonitor (atomics)
- ParameterManager (lock-free queue)
- MeteringManager (lock-free buffer)

**Locked Modules:**
- TrackMappingManager (CriticalSection)
- PluginManager (CriticalSection)
- ClipSynchronizer (CriticalSection)

**Delegating Modules:**
- PluginWindowBridge (delegates to PluginWindowManager)
- AudioRoutingManager (uses TE's thread-safe APIs)
- MidiRoutingManager (uses TE's thread-safe APIs)

---

## Success Metrics

✅ **Code Quality**
- [ ] AudioBridge reduced to ~500 LOC
- [ ] All modules < 500 LOC
- [ ] Each module single responsibility
- [ ] Clear module boundaries

✅ **Functional**
- [ ] All existing functionality preserved
- [ ] All tests pass
- [ ] No behavior changes

✅ **Performance**
- [ ] No audio dropouts
- [ ] No increased latency
- [ ] Benchmark results unchanged

✅ **Testing**
- [ ] Unit tests for each module
- [ ] Integration tests passing
- [ ] Thread-safety verified

✅ **Documentation**
- [ ] Each module documented
- [ ] Threading model clear
- [ ] API contracts defined

---

## Files Created

```
docs/issues/
├── README.md                      # Purpose of this directory
├── audiobridge-refactoring.md     # Comprehensive technical plan
├── HOW_TO_CREATE_ISSUE.md        # Guide for creating GitHub issue
└── ARCHITECTURE_DIAGRAM.md        # This file - visual overview
```

Also updated:
- `GITHUB_ISSUE_TEMPLATES.md` - Added Issue #4 summary

---

## Next Steps

1. **Review** - Team reviews and approves approach
2. **Phase 1** - Extract low-risk data managers
3. **Validate** - Ensure tests pass, no regressions
4. **Phase 2-4** - Continue with increasing complexity
5. **Document** - Update module docs as extracted
6. **Celebrate** - Maintainable, testable, AI-friendly code! 🎉
