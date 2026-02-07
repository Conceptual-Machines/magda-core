# Refactor: AudioBridge into Focused Modules

**Labels**: `refactoring`, `architecture`, `code-quality`, `audio`

## Problem Statement

The `AudioBridge` class has grown to 3,592 total lines (657 header + 2,935 implementation) with **70+ methods** and **19 distinct areas of responsibility**. This exceeds reasonable context size for both human developers and AI assistants, making the code difficult to maintain, test, and extend.

## Current Structure Analysis

### File Metrics
- **Total Lines**: 3,592 (657 header + 2,935 implementation)
- **Methods**: 70+ public/private methods
- **Member Variables**: 60+ fields
- **Responsibilities**: 19 distinct functional areas

### Current Responsibilities (from AudioBridge.hpp sections)

1. **TrackManagerListener implementation** - Track change notifications
2. **ClipManagerListener implementation** - Clip change notifications
3. **Clip Synchronization (Arrangement)** - Arrangement clip lifecycle
4. **Session Clip Lifecycle** - Slot-based session clips
5. **Transient Detection** - Audio analysis
6. **Warp Markers** - Time-stretching markers
7. **Plugin Loading** - Built-in and external plugin management
8. **Track Mapping** - Track ID to TE track mapping
9. **Metering** - Audio level monitoring
10. **Parameter Queue** - Lock-free parameter communication
11. **Synchronization** - Full system sync
12. **Audio Callback Support** - Real-time processing
13. **Transport State** - Playback state management
14. **MIDI Activity Monitoring** - MIDI event tracking
15. **Mixer Controls** - Volume/pan controls
16. **Master Metering** - Master channel levels
17. **Audio Routing** - Audio I/O routing
18. **MIDI Routing** - MIDI device routing
19. **Plugin Window Manager** - UI window delegation

## Why This Needs Refactoring

### 1. **Context Overflow**
- Exceeds typical LLM context windows (Claude: ~200k tokens ≈ 5,000 LOC)
- Makes AI-assisted development difficult
- Human developers must keep 19 different concerns in working memory

### 2. **Testing Complexity**
- Difficult to unit test individual responsibilities
- Mock setup requires simulating entire audio engine
- Integration tests are monolithic

### 3. **Thread Safety Complexity**
- Multiple threading contexts (UI, audio, message thread)
- Lock management spread across many concerns
- Difficult to verify lock-free correctness

### 4. **Single Responsibility Principle Violation**
- God object antipattern
- Changes to one feature risk breaking unrelated features
- Difficult to reason about side effects

### 5. **Coupling Issues**
- High internal coupling between unrelated features
- Changes ripple unpredictably
- Refactoring is risky

## Proposed Module Decomposition

Break AudioBridge into focused, single-responsibility modules:

### Core Coordination Layer
**AudioBridge (Coordinator)** - Thin facade coordinating modules
- Owns module instances
- Delegates to appropriate modules
- Minimal state, no direct TE interaction

### Mapping & Synchronization Modules

**1. TrackMappingManager**
- Track ID ↔ TE AudioTrack mapping
- Track creation/removal
- Track property synchronization
- ~300 LOC estimated

**2. PluginManager**
- Device ID ↔ TE Plugin mapping
- Plugin loading (built-in & external)
- Plugin lifecycle management
- DeviceProcessor ownership
- ~400 LOC estimated

**3. ClipSynchronizer**
- Arrangement clip sync
- Session clip slot management
- Clip ID mapping (MAGDA ↔ TE)
- Launch/stop operations
- ~500 LOC estimated

### Audio Processing Modules

**4. MeteringManager**
- Track level metering
- Master channel metering
- Lock-free metering buffer management
- ~300 LOC estimated

**5. ParameterManager**
- Parameter queue management
- Parameter change dispatch
- Lock-free parameter communication
- ~200 LOC estimated

**6. MixerController**
- Volume/pan controls (track & master)
- VolumeAndPan plugin positioning
- ~250 LOC estimated

### Routing Modules

**7. AudioRoutingManager**
- Audio input/output routing
- Route management
- ~200 LOC estimated

**8. MidiRoutingManager**
- MIDI input routing
- MIDI device management
- Pending route handling
- ~250 LOC estimated

### Specialized Feature Modules

**9. TransportStateManager**
- Transport playing state
- Just-started/just-looped flags
- Lock-free state management
- ~150 LOC estimated

**10. MidiActivityMonitor**
- MIDI activity tracking
- Per-track activity flags
- Lock-free monitoring
- **FIX**: Use dynamic container instead of fixed 128-track array
- ~200 LOC estimated

**11. WarpMarkerManager**
- Transient detection
- Warp marker CRUD operations
- WarpTimeManager interaction
- ~300 LOC estimated

**12. PluginWindowBridge**
- Plugin UI window delegation
- Window open/close/toggle
- PluginWindowManager coordination
- ~150 LOC estimated

## Implementation Strategy

### Phase 1: Extract Pure Data Managers (Low Risk)
1. TransportStateManager
2. MidiActivityMonitor
3. ParameterManager

These are mostly data holders with minimal TE interaction.

### Phase 2: Extract Independent Feature Modules (Medium Risk)
4. WarpMarkerManager
5. PluginWindowBridge
6. MixerController

These have clear boundaries and limited cross-dependencies.

### Phase 3: Extract Core Mapping Modules (Higher Risk)
7. TrackMappingManager
8. PluginManager
9. ClipSynchronizer

These are central to AudioBridge but can be extracted with careful interface design.

### Phase 4: Extract Routing & Metering (Highest Risk)
10. AudioRoutingManager
11. MidiRoutingManager
12. MeteringManager

These involve complex TE interactions and real-time constraints.

## Benefits

### Immediate Benefits
- **Testability**: Each module can be unit tested in isolation
- **Clarity**: Clear boundaries and responsibilities
- **Maintainability**: Changes localized to specific modules
- **AI-Friendly**: Each module fits in LLM context windows

### Long-Term Benefits
- **Extensibility**: New features added to appropriate modules
- **Performance**: Easier to optimize individual concerns
- **Thread Safety**: Easier to verify lock-free correctness per module
- **Documentation**: Each module can have focused documentation

## Success Criteria

1. ✅ AudioBridge becomes a thin coordinator (~500 LOC)
2. ✅ Each module has single, clear responsibility
3. ✅ Each module is independently testable
4. ✅ No module exceeds 500 LOC
5. ✅ Thread safety is clear and documented per module
6. ✅ All existing functionality preserved
7. ✅ All tests pass
8. ✅ No performance regression

## Testing Strategy

For each extracted module:
1. Write unit tests for new module interface
2. Extract module with tests passing
3. Update AudioBridge to delegate to module
4. Verify integration tests still pass
5. Run performance benchmarks

## Risks & Mitigation

### Risk 1: Breaking Thread Safety
**Mitigation**: 
- Extract lock-free modules first (transport, MIDI activity)
- Document threading model for each module
- Add thread-safety assertions

### Risk 2: Performance Regression
**Mitigation**:
- Run audio processing benchmarks before/after
- Profile critical paths
- Use move semantics for data transfer

### Risk 3: Incomplete Separation
**Mitigation**:
- Start with most independent modules
- Iterate on interfaces
- Allow some shared state initially, refine later

### Risk 4: Breaking Existing Code
**Mitigation**:
- Maintain existing public API initially
- Use delegation pattern
- Comprehensive integration testing

## Notes

- This is architectural refactoring, not a feature addition
- Focus on code organization, not behavior changes
- Preserve all existing functionality
- Maintain backward compatibility during transition

## Related Issues

- Issue #1: MIDI activity tracking fails for track IDs >= 128 (should be fixed in MidiActivityMonitor)
- See `BUG_ANALYSIS_REPORT.md` for identified issues in AudioBridge

## References

- Original file: `magda/daw/audio/AudioBridge.{hpp,cpp}`
- Project refactoring standards: `.github/workflows/refactoring-scanner.yml`
- Lizard complexity threshold: CCN > 15
- File size threshold: > 800 lines
