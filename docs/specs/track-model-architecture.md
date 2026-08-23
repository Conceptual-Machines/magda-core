# Track Model Architecture Specification

## Core Principle

**Separate backend (data model) from context views (presentation).**

The track model is the single source of truth. Views are lenses that present and interact with that data differently based on context (Arrange, Mix, Live) and optional device UIs.

```
┌─────────────────────────────────────────────────────┐
│                     BACKEND                         │
│  Track Model / Routing / MIDI / Audio / Automation  │
└───────────────────────┬─────────────────────────────┘
                        │
        ┌───────────────┼───────────────┐
        ▼               ▼               ▼
   ┌─────────┐    ┌──────────┐    ┌──────────┐
   │ ARRANGE │    │   MIX    │    │   LIVE   │
   │  View   │    │   View   │    │   View   │
   └────┬────┘    └────┬─────┘    └────┬─────┘
        │              │               │
        └──────────────┼───────────────┘
                       ▼
              ┌─────────────────┐
              │ DEVICES (UI)    │
              │ Drum Rack, etc  │
              └─────────────────┘
```

---

## Part 1: Backend Track Model

### 1.1 Track Types

```cpp
enum class TrackType {
    Media = 0,     // Clips of any kind, instruments, effects, input
    Group = 3,     // Contains child tracks, routing hub
    Aux = 4,       // Receives from sends
    Master = 5,    // Final output
    MultiOut = 6,  // Output track for a multi-out instrument pair
    Chord = 7      // Singleton chord-progression track, monitor-only
};
```

This began as `Audio`, `Instrument` and `MIDI` as three separate types. They
collapsed into one hybrid track, which kept the name `Audio` and so read as a
statement about content for years after it had stopped being one; it is `Media`
now. The ordinals that Instrument and MIDI used are skipped rather than reused,
because the numbers are written into every `.mgd` and are pinned
(`tests/test_persisted_enum_pins.cpp`).

What each type can do is declared once in `TrackTypeTraits`, not re-derived per
call site. See `magda/daw/core/TrackTypes.hpp`.

### 1.2 Track Hierarchy

Tracks form a tree structure. Any track can contain child tracks, but `Group` is the semantic type for explicit grouping.

```cpp
struct Track {
    TrackId id;
    TrackType type;
    std::string name;

    // Hierarchy
    TrackId parentId;                    // null for root-level tracks
    std::vector<TrackId> childIds;       // empty for leaf tracks

    // Content
    std::vector<Clip> clips;             // MIDI or audio clips
    std::vector<Device> devices;         // plugins, effects, rack UIs

    // Routing
    RoutingConfig routing;               // for groups: how MIDI routes to children
    AudioRouting audioRouting;           // output bus, sends

    // View settings (per-view metadata)
    std::map<ViewMode, TrackViewSettings> viewSettings;
};
```

### 1.3 Track Hierarchy Example

```
Project
├── Drums (Group)
│   ├── Kick (Audio)
│   ├── Snare (Audio)
│   └── Cymbals (Group)
│       ├── HiHat (Audio)
│       └── Crash (Audio)
├── Bass (Instrument)
├── Synths (Group)
│   ├── Lead (Instrument)
│   └── Pad (Instrument)
└── Master (Master)
```

---

## Part 2: View Context System

### 2.1 View Modes

```cpp
enum class ViewMode {
    Live,       // Performance, clip launching
    Arrange,    // Timeline composition
    Mix,        // Mixing, levels, routing
    Master      // Mastering, final output
};
```

### 2.2 Track View Settings

Each track has settings that vary per view:

```cpp
struct TrackViewSettings {
    bool visible = true;        // Show in this view
    bool locked = false;        // Prevent editing in this view
    bool collapsed = false;     // For groups: collapse children
    int height = 80;            // Track height in arrange view
    float faderPosition = 0.0f; // Saved fader position (Mix view)
};
```

### 2.3 View Behaviors

| Property | Live | Arrange | Mix | Master |
|----------|------|---------|-----|--------|
| Show groups collapsed | Maybe | Yes | No | No |
| Show individual tracks | Maybe | Maybe | Yes | No |
| Show master only | No | No | Maybe | Yes |
| Edit MIDI | No | Yes | No | No |
| Adjust levels | Yes | No | Yes | Yes |

### 2.4 Inheritance Rules

When a group's view settings change:

```cpp
// If group is hidden, children are implicitly hidden
// If group is collapsed, children don't render but exist
// If group is locked, children inherit lock (can override)

struct ViewSettingsInheritance {
    bool visible;     // AND with parent (hidden parent = hidden child)
    bool collapsed;   // Parent only (children just don't render)
    bool locked;      // OR with parent (locked parent = locked child, unless override)
};
```

### 2.5 View Context Controller

```cpp
class ViewContextController {
public:
    // Get effective settings (with inheritance applied)
    TrackViewSettings getEffectiveSettings(TrackId track, ViewMode view);

    // Update settings for a track in a view
    void setVisible(TrackId track, ViewMode view, bool visible);
    void setLocked(TrackId track, ViewMode view, bool locked);
    void setCollapsed(TrackId track, ViewMode view, bool collapsed);

    // Bulk operations
    void showOnlyInView(TrackId track, ViewMode view);      // Hide in all other views
    void showInAllViews(TrackId track);
    void collapseAllGroups(ViewMode view);
    void expandAllGroups(ViewMode view);

    // Query
    std::vector<TrackId> getVisibleTracks(ViewMode view);   // For rendering
    bool isEffectivelyVisible(TrackId track, ViewMode view); // With inheritance
};
```

---

## Part 3: Group Tracks & MIDI Routing

### 3.1 Routing Types

Groups can route MIDI to children in multiple ways:

```cpp
enum class RoutingType {
    None,       // Pass-through (all MIDI to all children)
    Fixed,      // Drum map style (note → track)
    Range,      // Note/velocity/channel ranges
    Script      // User-defined logic
};

struct RoutingConfig {
    RoutingType type = RoutingType::None;

    // For Fixed routing
    std::map<int, TrackId> noteToTrack;  // MIDI note → child track

    // For Range routing
    std::vector<RangeRule> rangeRules;

    // For Script routing
    std::string scriptCode;              // Lua script
};
```

### 3.2 Range Rules

```cpp
struct RangeRule {
    TrackId targetTrack;

    // Note range (optional)
    std::optional<int> noteMin;
    std::optional<int> noteMax;

    // Velocity range (optional)
    std::optional<int> velMin;
    std::optional<int> velMax;

    // Channel filter (optional)
    std::optional<int> channel;

    // Priority (higher = checked first)
    int priority = 0;
};
```

### 3.3 Script Routing

```lua
-- Script has access to note properties
-- Returns track name(s) or nil for no routing

function route(note, velocity, channel, time)
    -- Drum map style
    if note == 36 then return "Kick" end
    if note == 38 then return "Snare" end

    -- Range style
    if note >= 60 and note <= 72 then return "Lead" end

    -- Velocity layers
    if velocity > 100 then
        return {"Hard Layer", "Transient Layer"}  -- Multiple targets
    end

    -- Probability
    if math.random() > 0.5 then return "Random A" end
    return "Random B"

    -- Time-based
    if time % 4 < 2 then return "A" else return "B" end

    -- Default
    return nil  -- Note is dropped
end
```

### 3.4 MIDI Routing Engine

```cpp
class MIDIRoutingEngine {
public:
    // Route a MIDI event through a group's config
    // Returns list of (targetTrack, modifiedEvent) pairs
    std::vector<RoutedEvent> route(
        TrackId groupTrack,
        const MIDIEvent& event,
        const RoutingConfig& config
    );

private:
    RoutedEvent routeFixed(const MIDIEvent& event, const RoutingConfig& config);
    RoutedEvent routeRange(const MIDIEvent& event, const RoutingConfig& config);
    RoutedEvent routeScript(const MIDIEvent& event, const RoutingConfig& config);

    ScriptEngine scriptEngine_;  // Lua interpreter
};
```

### 3.5 Unified MIDI View

When composing in a group track's piano roll:

- All child track notes shown in one view
- Color-coded by destination track
- Editing routes through the routing engine
- New notes use current routing config to determine destination

```cpp
struct UnifiedMIDIView {
    TrackId groupTrack;

    // Get all notes from children, tagged with source
    std::vector<TaggedNote> getAllNotes();

    // Add note - routing determines destination
    void addNote(const MIDINote& note);

    // Move note - may change destination based on new pitch
    void moveNote(NoteId note, int newPitch, int newTime);

    // Visual
    Colour getTrackColour(TrackId child);
};
```

---

## Part 4: Devices as UI Controllers

### 4.1 Device Concept

Devices are UI components that live on tracks and provide interfaces for interaction. Some are audio processors (plugins), others are pure UI controllers.

```cpp
enum class DeviceCategory {
    Instrument,     // Generates audio from MIDI
    Effect,         // Processes audio
    MIDI,           // Processes MIDI
    Controller      // UI only, controls track/routing (Drum Rack, etc.)
};

struct Device {
    DeviceId id;
    DeviceCategory category;
    std::string name;

    // For plugins
    PluginId pluginId;
    PluginState state;

    // For controller devices
    ControllerType controllerType;  // DrumRack, ChordMap, ScriptRouter, etc.
};
```

### 4.2 Controller Devices

Controller devices provide alternative UIs for interacting with group tracks and their routing.

```cpp
enum class ControllerType {
    DrumRack,       // Pad grid, sample dropping
    ChordMap,       // Chord trigger grid
    RangeSplit,     // Keyboard zone editor
    ScriptRouter,   // Code editor for routing scripts
    StepSequencer   // Step-based pattern editor
};
```

### 4.3 Drum Rack Device

```cpp
class DrumRackDevice : public ControllerDevice {
public:
    // Grid configuration
    void setGridSize(int rows, int cols);  // e.g., 4x4, 4x8

    // Pad management
    struct Pad {
        int note;           // MIDI note this pad responds to
        TrackId track;      // Child track this pad controls
        Colour colour;
        std::string label;
    };

    std::vector<Pad> getPads();

    // Actions
    void triggerPad(int index);                           // Preview/audition
    void assignSampleToPad(int index, const File& sample); // Creates child track
    void assignTrackToPad(int index, TrackId track);       // Link existing track

    // This device reads/writes the parent group's RoutingConfig
    void syncFromRouting(const RoutingConfig& config);
    RoutingConfig syncToRouting();

private:
    int rows_ = 4;
    int cols_ = 4;
    std::vector<Pad> pads_;
};
```

### 4.4 Device ↔ Routing Sync

Controller devices are a UI layer over the routing config:

```
┌─────────────────────────────────────────┐
│           Drum Rack Device (UI)         │
│  ┌────┬────┬────┬────┐                  │
│  │ C1 │ D1 │ E1 │ F1 │  ← Pad grid      │
│  └────┴────┴────┴────┘                  │
└─────────────────┬───────────────────────┘
                  │ sync
                  ▼
┌─────────────────────────────────────────┐
│         RoutingConfig (Backend)         │
│  noteToTrack: {                         │
│    36: Kick,                            │
│    38: Snare,                           │
│    40: Clap,                            │
│    41: HiHat                            │
│  }                                      │
└─────────────────────────────────────────┘
                  │ routes
                  ▼
┌─────────────────────────────────────────┐
│           Child Tracks                  │
│  Kick │ Snare │ Clap │ HiHat            │
└─────────────────────────────────────────┘
```

---

## Part 5: Integration Examples

### 5.1 Drum Kit Workflow

**Setup:**
1. Create Group track "Drums"
2. Add Drum Rack device to group
3. Drag samples onto pads → creates child Audio tracks + routing

**Compose (Arrange view):**
- Group collapsed, shows unified piano roll
- Write drum pattern, notes color-coded
- Only "Drums" lane visible

**Mix (Mix view):**
- All child tracks expanded
- Individual faders for Kick, Snare, HiHat, etc.
- Group track is the drum bus

### 5.2 Orchestra Section Workflow

**Setup:**
1. Create Group track "Strings"
2. Add Range Split device
3. Configure: C0-B2 → Cellos, C3-B4 → Violas, C5-B6 → Violins

**Compose:**
- Write in Strings group lane
- Notes auto-route to appropriate section
- See all parts in unified view

**Mix:**
- Cellos, Violas, Violins as separate faders
- Strings group as section bus

### 5.3 Generative Routing Workflow

**Setup:**
1. Create Group track "Generative"
2. Add Script Router device
3. Write routing script with probability/time-based logic

**Play:**
- Input MIDI routes dynamically
- Different results each time
- Great for live performance

---

## Part 6: Data Persistence

### 6.1 Project File Structure

```
project.magda
├── tracks/
│   ├── track_001.json      # Track metadata, routing config
│   ├── track_002.json
│   └── ...
├── clips/
│   ├── clip_001.midi
│   ├── clip_002.wav
│   └── ...
├── devices/
│   ├── device_001.json     # Device state (including controller UI state)
│   └── ...
├── views/
│   ├── arrange.json        # View-specific settings
│   ├── mix.json
│   ├── live.json
│   └── master.json
└── project.json            # Project metadata, track tree structure
```

### 6.2 View Settings Persistence

View settings are saved separately so they can be reset/shared independently:

```json
// views/arrange.json
{
  "viewMode": "Arrange",
  "trackSettings": {
    "track_001": { "visible": true, "collapsed": true, "height": 80 },
    "track_002": { "visible": false },
    "track_003": { "visible": true, "locked": true }
  }
}
```

---

## Part 7: Implementation Phases

### Phase 1: Core Track Model
- Track struct with hierarchy
- TrackManager for CRUD operations
- Basic parent/child relationships

### Phase 2: View Context System
- ViewMode enum
- TrackViewSettings struct
- ViewContextController
- Per-view visibility/lock/collapse

### Phase 3: Group Routing - Fixed
- RoutingConfig with Fixed type
- Note-to-track mapping
- Basic MIDI routing engine

### Phase 4: Group Routing - Range & Script
- Range rules
- Script engine integration (Lua)
- Full routing flexibility

### Phase 5: Unified MIDI View
- Piano roll shows all children
- Color coding
- Edit through routing

### Phase 6: Controller Devices
- Device base class
- Drum Rack implementation
- Device ↔ Routing sync

### Phase 7: Additional Controllers
- Chord Map
- Range Split editor
- Script Router UI

---

## Summary

| Layer | Responsibility | Changes Per |
|-------|---------------|-------------|
| Backend (Track Model) | Data, hierarchy, routing logic | Project edits |
| View Context | Visibility, collapse, lock per view | View mode switch |
| Controller Devices | UI for interacting with routing | User preference |

**Key Insight:** The track hierarchy IS the routing. Groups aren't a workaround device - they're first-class tracks that happen to contain other tracks and define how MIDI flows to them.

**Flexibility:** Same backend supports drum rack workflow, orchestral sections, generative routing, and anything in between. Views and devices are just different ways to see and interact with the same data.
