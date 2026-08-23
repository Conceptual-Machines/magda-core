# MAGDA Test Plan - 2026-01-25

## Recent Fixes to Verify

### 1. MIDI Activity Monitor ✅
**What was fixed**: MIDI routing now properly initialized for all default tracks at startup

**Test Steps**:
```bash
make run-console
```

1. **Launch MAGDA** - Check console output for:
   ```
   Synced MIDI routing for track 1: all
   Synced MIDI routing for track 2: all
   ...
   Synced MIDI routing for track 11: all
   ```

2. **Play MIDI keyboard** - Verify:
   - All 11 tracks show MIDI activity indicators lighting up
   - Activity indicators fade out smoothly after note off
   - Sound is produced correctly

3. **Check console** for MIDI debug messages:
   ```
   MidiBridge: Note ON received - note=X vel=Y from 'Device Name'
     -> Checking track 11: routing='all' vs source='...' -> MATCH
     -> Checking track 10: routing='all' vs source='...' -> MATCH
     ...
   ```

**Expected Behavior**:
- ✅ All tracks light up (correct - they all have "All MIDI input" by default)
- ✅ To change: select specific MIDI device per track instead of "All MIDI input"

### 2. Shutdown Crash Fix ✅
**What was fixed**: Proper cleanup of JUCE Drawable objects to prevent double-delete crashes

**Test Steps**:
```bash
make run-console
```

1. **Start MAGDA**
2. **Use the application**:
   - Load plugins
   - Play MIDI
   - Switch view modes (Live, Arrange, Mix, Master)
   - Adjust mixer faders
   - Open plugin UIs

3. **Quit normally** (Cmd+Q or menu → Quit)
   - Should exit cleanly ✅
   - No "malloc_zone_error" in console ✅
   - No "abort() called" ✅
   - No crash dialog ✅

4. **Repeat 3-5 times** to ensure consistency

**What to watch for**:
- ❌ NO crash on quit
- ❌ NO memory errors in console
- ✅ Clean shutdown every time

### 3. Plugin UI Loading
**Status**: Already implemented, verify it works

**Test Steps**:
```bash
make run-console
```

1. **Load an external plugin** (VST3 or AU):
   - Open plugin browser (left panel)
   - Load a synth or FX plugin (e.g., Serum, Vital, any VST3/AU)

2. **Open plugin window**:
   - Find device in track chain panel
   - Click "open in new" icon button in device header
   - Plugin native editor window should appear
   - Button should turn blue (active state)

3. **Close plugin window**:
   - Click button again → window closes, button returns to normal
   - Or close window manually → button updates to inactive

4. **Multiple plugins**:
   - Open multiple plugin windows simultaneously
   - Verify all work correctly

**Expected**: ✅ Plugin windows open/close correctly, button states update

### 4. Performance Profiling (Optional)
**Status**: Available but disabled by default

**Test Steps**:
```bash
make run-profile
```

Check console for performance metrics:
```
[PROFILE] PluginLoad: 145.23 ms
[PROFILE] UIFrame: 2.34 ms
[PROFILE] ParamChanges: 0.45 ms
```

**Performance Targets**:
- Plugin load: < 200ms (warning > 500ms)
- UI frame: < 16ms (warning > 20ms)
- Param changes: < 1ms (warning > 5ms)

## Comprehensive System Test

### Audio Engine
- [ ] Start/stop playback
- [ ] Record audio
- [ ] Record MIDI
- [ ] Audio device switching
- [ ] Sample rate changes
- [ ] Buffer size changes

### MIDI System
- [ ] MIDI input routing
- [ ] MIDI activity monitoring
- [ ] MIDI device hot-plugging
- [ ] MIDI learn for parameters
- [ ] Multiple MIDI devices

### Track Management
- [ ] Create new tracks
- [ ] Delete tracks
- [ ] Rename tracks
- [ ] Arm/disarm recording
- [ ] Solo/mute tracks
- [ ] Track routing changes

### Plugin System
- [ ] Load VST3 plugins
- [ ] Load AU plugins
- [ ] Open plugin UIs
- [ ] Plugin parameter automation
- [ ] Plugin state saving (if implemented)
- [ ] Plugin hot-swapping

### FX Chain
- [ ] Add devices to chain
- [ ] Remove devices from chain
- [ ] Reorder devices (drag/drop)
- [ ] Device bypass
- [ ] Macro controls
- [ ] Modulators (LFO, envelope followers)

### Mixer View
- [ ] Volume faders
- [ ] Pan controls
- [ ] Send controls
- [ ] Meter display
- [ ] Routing selector
- [ ] Master fader

### Transport & Timeline
- [ ] Play/pause/stop
- [ ] Loop region
- [ ] Tempo changes
- [ ] Time signature changes
- [ ] Timeline zoom
- [ ] Timeline scrolling

### UI Views
- [ ] Live view
- [ ] Arrange view
- [ ] Mix view
- [ ] Master view
- [ ] Switch between views smoothly

### Memory & Stability
- [ ] **Launch → Quit (10 times)** - No crashes ✅
- [ ] **Long session (30+ min)** - No memory leaks
- [ ] **Heavy plugin usage** - Stable performance
- [ ] **Multiple audio/MIDI devices** - No conflicts

## Regression Testing

### Known Fixed Issues
- [x] Issue #51: Track routing
- [x] Issue #81: Audio/MIDI device abstraction
- [x] MIDI activity monitor initialization
- [x] Shutdown crash (Drawable double-delete)
- [x] FooterBar button cleanup
- [x] SvgButton Drawable cleanup
- [x] MixerLookAndFeel Drawable cleanup

### Areas to Watch
- [ ] Automation recording/playback
- [ ] Plugin state persistence
- [ ] Project save/load
- [ ] Undo/redo
- [ ] Parameter modulation links

## Performance Benchmarks

### Startup Time
- [ ] Cold start: < 3 seconds
- [ ] Warm start: < 2 seconds

### Audio Performance
- [ ] Audio callback: < 10ms @ 512 samples
- [ ] Zero glitches during normal operation
- [ ] Low CPU usage at idle (< 5%)

### UI Responsiveness
- [ ] Button clicks: immediate feedback
- [ ] Fader dragging: smooth, no lag
- [ ] Window resizing: smooth
- [ ] View switching: < 100ms

## Stress Testing

### Plugin Loading
- [ ] Load 10 plugins rapidly
- [ ] Open all plugin UIs
- [ ] Close all plugin UIs
- [ ] Remove all plugins
- [ ] No crashes, no memory errors

### Track Creation
- [ ] Create 20 tracks
- [ ] Load plugins on all tracks
- [ ] Play MIDI on all tracks
- [ ] Solo/mute all tracks
- [ ] Delete all tracks

### Automation
- [ ] Record automation on 10 parameters
- [ ] Playback automation
- [ ] Edit automation curves
- [ ] Clear automation

## Bug Reporting Template

If you find a bug, report with:

1. **Steps to reproduce**
2. **Expected behavior**
3. **Actual behavior**
4. **Console output** (if any errors)
5. **Crash log** (if crashed)
6. **System info**: macOS version, build type (debug/release)

## Test Results

### Build Info
- **Date**: 2026-01-25
- **Commit**: [git hash]
- **Build type**: Debug
- **JUCE version**: 8.0.10

### Test Summary
- [ ] All critical tests pass
- [ ] No regressions found
- [ ] Performance acceptable
- [ ] Memory usage stable
- [ ] Ready for next milestone

---

**Tester Notes**:

_Add your observations here..._
