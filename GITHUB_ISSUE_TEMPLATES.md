# GitHub Issue Templates

Copy these templates to create issues manually on GitHub.

---

## Issue #1: CRITICAL: MIDI activity tracking fails for track IDs >= 128

**Labels**: `bug`, `critical`, `audio`

### Description

The `AudioBridge` class uses a fixed-size array to track MIDI activity indicators, but track IDs can exceed this limit, causing silent failures.

### Bug Details

**Location**: 
- `magda/daw/audio/AudioBridge.hpp` lines 534-535
- `magda/daw/audio/AudioBridge.cpp` lines 1013-1024

**Root Cause**:
```cpp
static constexpr int kMaxTracks = 128;
std::array<std::atomic<bool>, kMaxTracks> midiActivityFlags_;

void AudioBridge::triggerMidiActivity(TrackId trackId) {
    if (trackId >= 0 && trackId < kMaxTracks) {  // Bounds check fails for trackId >= 128
        midiActivityFlags_[trackId].store(true, std::memory_order_release);
    }
}
```

Track IDs in `TrackManager` are generated using `nextTrackId_++` which can grow beyond 128.

### Reproduction Steps

1. Create 100 tracks (track IDs 1-100)
2. Delete 90 tracks (track IDs are not reused)
3. Create 50 more tracks (track IDs 101-150)
4. Observe that tracks with IDs >= 128 do not show MIDI activity indicators

**Expected**: MIDI activity should work for all valid track IDs  
**Actual**: MIDI activity silently fails for track IDs >= 128

### Impact

- MIDI activity indicators don't work for tracks with IDs >= 128
- Silent failure - no error message or crash
- Confusing user experience (feature appears broken for some tracks)
- Realistic scenario for power users working on large projects

### Suggested Fix

Replace fixed-size array with dynamic container:

```cpp
// Option 1: Use unordered_map (preferred)
std::unordered_map<TrackId, std::atomic<bool>> midiActivityFlags_;

// Option 2: Use map for ordered access
std::map<TrackId, std::atomic<bool>> midiActivityFlags_;
```

Update methods to work with map:
```cpp
void AudioBridge::triggerMidiActivity(TrackId trackId) {
    if (trackId >= 0) {
        midiActivityFlags_[trackId].store(true, std::memory_order_release);
    }
}

bool AudioBridge::consumeMidiActivity(TrackId trackId) {
    if (trackId >= 0 && midiActivityFlags_.find(trackId) != midiActivityFlags_.end()) {
        return midiActivityFlags_[trackId].exchange(false, std::memory_order_acq_rel);
    }
    return false;
}
```

### Test Case

See `tests/test_audiobridge_track_limit_bug.cpp` for documented test case.

### Additional Info

- See `BUG_ANALYSIS_REPORT.md` for comprehensive analysis
- Severity: HIGH
- Confidence: 100% (definite bug)

---

## Issue #2: Fragile plugin cleanup ordering in AudioBridge::syncTrackPlugins

**Labels**: `bug`, `code-quality`, `refactoring`, `audio`

### Description

The plugin cleanup code in `AudioBridge::syncTrackPlugins()` has a fragile ordering when working with bidirectional maps. While currently safe, it could easily become a bug if refactored.

### Bug Details

**Location**: 
- `magda/daw/audio/AudioBridge.cpp` around line 917 in `syncTrackPlugins()`

**Current Code** (works but fragile):
```cpp
auto it = deviceToPlugin_.find(deviceId);
if (it != deviceToPlugin_.end()) {
    auto plugin = it->second;
    pluginToDevice_.erase(plugin.get());  // Using raw pointer from plugin.get()
    deviceToPlugin_.erase(it);
    plugin->deleteFromParent();
}
```

**Problem**:
The code uses `plugin.get()` to obtain a raw pointer which is used as a map key. This is currently safe because the `plugin` local variable holds a reference count, but if someone refactors it like this:

```cpp
// This would be BROKEN:
pluginToDevice_.erase(it->second.get());  // Potential use-after-free
deviceToPlugin_.erase(it);
it->second->deleteFromParent();
```

### Impact

- **Current**: No impact, works correctly
- **If refactored**: Potential crash, memory corruption, or use-after-free
- **Risk**: Medium - easy to break during code maintenance
- **Debugging**: Hard to debug if it breaks (may only manifest under specific timing)

### Suggested Fixes

**Option 1: Store raw pointer explicitly** (clearest intent):
```cpp
auto plugin = it->second;
auto* pluginPtr = plugin.get();  // Make it explicit
pluginToDevice_.erase(pluginPtr);
deviceToPlugin_.erase(it);
plugin->deleteFromParent();
```

**Option 2: Safer ordering** (erase ownership map first):
```cpp
auto plugin = it->second;
deviceToPlugin_.erase(it);  // Remove from ownership map first
pluginToDevice_.erase(plugin.get());  // Now clearly safe
plugin->deleteFromParent();
```

### Test Case

See `tests/test_audiobridge_plugin_cleanup_bug.cpp` for:
- Documentation of the pattern
- Explanation of why it's fragile
- Best practices for bidirectional maps

### Additional Info

- See `BUG_ANALYSIS_REPORT.md` for detailed analysis
- Severity: LOW (currently works)
- Risk: MEDIUM (could break if refactored)
- Confidence: 80%

---

## Issue #3: ClipManager::getClip() returns pointers that can be invalidated

**Labels**: `bug`, `design`, `memory-safety`

### Description

`ClipManager::getClip()` returns raw pointers to elements in a `std::vector`. These pointers become dangling if the vector is modified, which is a classic iterator invalidation bug.

### Bug Details

**Location**: 
- `magda/daw/core/ClipManager.cpp` lines 392-404

**Code**:
```cpp
ClipInfo* ClipManager::getClip(ClipId clipId) {
    auto it = std::find_if(clips_.begin(), clips_.end(),
                           [clipId](const ClipInfo& c) { return c.id == clipId; });
    return (it != clips_.end()) ? &(*it) : nullptr;  // Returns pointer to vector element
}

void ClipManager::deleteClip(ClipId clipId) {
    // ...
    clips_.erase(it);  // ❌ Invalidates ALL pointers returned by getClip()!
    notifyClipsChanged();
}
```

### Dangerous Pattern

```cpp
auto* clip1 = getClip(1);  // Get pointer to clip 1
auto* clip2 = getClip(2);  // Get pointer to clip 2

// Any operation that modifies clips_ invalidates pointers:
createClip(...);  // Vector might reallocate
deleteClip(3);    // Vector elements shift

// clip1 and clip2 are now dangling pointers! ❌
clip1->name = "foo";  // Crash or memory corruption
```

### Current Status

**Currently SAFE** because:
- All current code gets the pointer and uses it immediately
- No code holds pointers across function calls
- Pattern is understood by current developers

**But FRAGILE** because:
- Easy to misuse if pattern is not understood
- No compiler warning when misused
- Could cause crashes, memory corruption, or silent data corruption
- Symptoms appear far from root cause (hard to debug)

### Real-World Scenario

This could break if someone writes:
```cpp
auto* clip = clipManager.getClip(clipId);
// Do some work that might trigger a callback...
someComplexOperation();  // Could call createClip() internally
// Now clip pointer is potentially dangling!
clip->length = newLength;  // ❌ May crash
```

### Suggested Fixes

**Option 1: Document the limitation** (minimal change):
```cpp
/**
 * @brief Get a pointer to a clip (TEMPORARY USE ONLY)
 * 
 * WARNING: The returned pointer is invalidated by ANY operation that
 * modifies clips_ (create, delete, or any operation causing reallocation).
 * Do NOT store this pointer. Use immediately and discard.
 * 
 * @return Pointer to clip, valid only until next clips_ modification
 */
ClipInfo* getClip(ClipId clipId);
```

**Option 2: Return by value** (safer but more copying):
```cpp
std::optional<ClipInfo> getClip(ClipId clipId) const;
```

**Option 3: Use stable container** (no invalidation):
```cpp
// Change clips_ from vector to map:
std::map<ClipId, ClipInfo> clips_;
// or use deque (pointers stable on push_back):
std::deque<ClipInfo> clips_;
```

### Recommendation

**Short term**: Add documentation warning (Option 1)  
**Long term**: Consider changing container if performance allows (Option 3)

### Additional Info

- See `BUG_ANALYSIS_REPORT.md` for comprehensive analysis
- Severity: MEDIUM
- Confidence: 90%
- Current Impact: None (currently safe)
- Future Risk: High (easy to introduce bugs)

---

## Summary

All three bugs have been documented with:
- ✅ Detailed descriptions
- ✅ Code examples
- ✅ Reproduction steps
- ✅ Impact assessments
- ✅ Suggested fixes
- ✅ Test cases (where applicable)
- ✅ Comprehensive analysis in `BUG_ANALYSIS_REPORT.md`

Priority for fixes:
1. **Bug #1** - Fix immediately (real user-facing bug)
2. **Bug #3** - Document soon (prevent future issues)
3. **Bug #2** - Improve when refactoring (minor code quality)
