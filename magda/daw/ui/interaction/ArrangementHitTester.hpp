#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <set>
#include <vector>

namespace magda::interaction {

// ============================================================================
// ARRANGEMENT HIT TESTER (#1718, #1719)
//
// One pure zone model for the arrangement's pointer surfaces, shared by the
// cursor chooser and (eventually, #1721) the mouseDown gesture dispatch.
//
// The invariant this file exists to enforce: the cursor must always
// truthfully predict what a click would do. That is only guaranteed when
// both are projections of the same hit test, computed here from plain-data
// snapshots — no component state, no singletons — so every zone rule is
// headless-testable (tests/test_arrangement_hit_tester.cpp).
//
// Two surfaces, matching the two components that own pointer events:
//
//   panelZone()  — TrackContentPanel space: time-selection edges/body take
//                  priority over everything (clips are hit-transparent under
//                  a selection), then clip passthrough, then the upper
//                  (clip/marquee) vs lower (time-selection) lane zones.
//   clipHit()    — ClipComponent-local space: fade handles, volume handle,
//                  lower-half time-selection band, resize edges, body.
//
// Cursor policy lives in panelCursor()/clipCursor() as flat lookup tables
// over those zones — the single place where "selected clip body shows a
// grab hand" style decisions are visible.
//
// CursorKind (not juce::MouseCursor) keeps this translation unit free of
// CursorManager/app resources; toJuceCursor() maps to real cursors and is
// compiled only into the app target (ArrangementCursorMap.cpp).
// ============================================================================

/** Cursor vocabulary for the arrangement surfaces. Mapped to real
 *  juce::MouseCursor / CursorManager cursors by toJuceCursor(). */
enum class CursorKind {
    Normal,
    LeftRightResize,
    UpDownResize,
    UpDownLeftRightResize,
    DraggingHand,
    PointingHand,
    Crosshair,
    IBeam,
    Copying,
    NoteDraw,  // CursorManager::getNoteDrawCursor()
    Erase,     // CursorManager::getEraseCursor()
    Blade,     // CursorManager::getBladeCursor()
};

/** Plain-bool modifier snapshot so tests don't need juce::ModifierKeys. */
struct ModifierSnapshot {
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
    bool command = false;

    static ModifierSnapshot from(const juce::ModifierKeys& mods) {
        return {mods.isShiftDown(), mods.isCtrlDown(), mods.isAltDown(), mods.isCommandDown()};
    }
};

// ============================================================================
// Panel surface (TrackContentPanel space)
// ============================================================================

/** Pixel distance from a time-selection boundary that counts as the edge
 *  (resize) zone. Canonical value — TrackContentPanel::isOnSelectionEdge
 *  uses this same constant. */
constexpr int SELECTION_EDGE_THRESHOLD = 8;

enum class PanelZone {
    None,                // outside any lane / non-selectable area
    SelectionEdgeLeft,   // within threshold of the selection start, on a selected track
    SelectionEdgeRight,  // within threshold of the selection end, on a selected track
    SelectionBody,       // inside an active time selection, on a selected track
    OverClip,            // a clip component owns this point (panel shows Normal)
    EmptyLaneUpper,      // empty upper half of a lane: marquee / draw-clip zone
    EmptyLaneLower,      // empty lower half of a lane: time-selection zone
};

/** Everything panelZone() needs, as plain data. Built per-query by the
 *  panel from its live state; built directly by tests. */
struct PanelSnapshot {
    // Active time selection (TimelineState::SelectionState semantics:
    // empty selectedTracks = all tracks).
    bool selectionActive = false;
    double selStartBeats = 0.0;
    double selEndBeats = 0.0;
    const std::set<int>* selectedTracks = nullptr;  // nullptr == empty == all

    // Beat <-> pixel mapping (TrackContentPanel::pixelToBeats/beatsToPixel).
    double pixelsPerBeat = 0.0;  // currentZoom; <= 0 degrades like the panel
    int leftPadding = 0;         // LayoutConfig::TIMELINE_LEFT_PADDING

    /** One visible lane. hitTop/hitHeight is the track's own vertical hit
     *  band (excludes its automation lanes, like getTrackIndexAtY); area is
     *  the full lane rectangle (getTrackLaneArea) used for the upper/lower
     *  midpoint and the selectable-area test. */
    struct Lane {
        int hitTop = 0;
        int hitHeight = 0;
        juce::Rectangle<int> area;
    };
    std::vector<Lane> lanes;

    // Whether a ClipComponent is under the query point (panel-level cursor
    // defers to the clip there). Computed by the caller: hit-testing child
    // components is the one thing that can't be plain data.
    bool clipAtPoint = false;
};

/** Track index whose hit band contains y, or -1 (getTrackIndexAtY). */
int laneIndexAtY(int y, const PanelSnapshot& s);

/** Full panel-space hit result: the winning zone plus the raw per-test
 *  facts the gesture code branches on (#1721). `insideSelection` is the
 *  plain rectangle test — unlike `zone`, it stays true when the point is
 *  also within the edge threshold. */
struct PanelHit {
    PanelZone zone = PanelZone::None;
    int laneIndex = -1;        // hit-band lane under y, or -1
    bool inUpperZone = false;  // lane's upper half (clip/marquee zone)
    bool selectable = false;   // inside any lane rectangle
    bool onSelectionEdge = false;
    bool selectionEdgeIsLeft = false;
    bool insideSelection = false;
};

PanelHit panelHit(int x, int y, const PanelSnapshot& s);

/** The winning zone for a point, using the same priority order as the
 *  panel's historical cursor logic: selection edge > selection body >
 *  clip > upper/lower lane zones. Convenience for panelHit(...).zone. */
PanelZone panelZone(int x, int y, const PanelSnapshot& s);

/** Cursor for a panel zone. The hover cursor is modifier-independent: the
 *  draw-clip (pen) cursor only appears once a Shift-draw actually begins, so
 *  Shift stays free for Shift+wheel horizontal scroll without the pen flashing
 *  on every Shift press. */
CursorKind panelCursor(PanelZone zone);

// ============================================================================
// Clip surface (ClipComponent-local space)
// ============================================================================

/** Clip geometry constants shared with ClipComponent (canonical here). */
namespace ClipMetrics {
constexpr int RESIZE_HANDLE_WIDTH = 6;
constexpr int HEADER_HEIGHT = 16;
constexpr int FADE_HANDLE_HIT_WIDTH = 14;
constexpr float VOLUME_LINE_HIT_DISTANCE = 6.0f;
}  // namespace ClipMetrics

enum class ClipZone {
    FadeIn,     // fade-in handle (selected audio clips)
    FadeOut,    // fade-out handle (selected audio clips)
    Volume,     // volume line (selected audio clips, away from edges/fades)
    LowerHalf,  // lower body half, away from edges: time-selection band
    EdgeLeft,   // resize edge
    EdgeRight,  // resize edge
    Body,       // everything else
};

/** Result of a clip-space hit test: the winning zone plus the raw per-zone
 *  flags (a point can be on an edge AND a fade handle; the zone applies the
 *  priority order, the flags feed hover painting). */
struct ClipHit {
    ClipZone zone = ClipZone::Body;
    bool onLeftEdge = false;
    bool onRightEdge = false;
    bool onFadeIn = false;
    bool onFadeOut = false;
    bool onVolume = false;
    bool lowerHalf = false;
};

/** Everything clipHit() needs, as plain data. The audio-only fields carry
 *  the *effective* fades (crossfade-aware, ClipComponent::computeEffectiveFades)
 *  and the clip's timeline length at the current tempo. */
struct ClipSnapshot {
    int width = 0;
    int height = 0;
    bool selected = false;
    bool isAudio = false;
    double fadeInSeconds = 0.0;
    double fadeOutSeconds = 0.0;
    double clipLengthSeconds = 0.0;
    float volumeGainLinear = 1.0f;  // jlimit(0,1, decibelsToGain(volumeDB))
};

/** Zone + hover flags for a clip-local point. Priority: fade handles >
 *  volume handle > lower half > edges > body, with the historical gating
 *  (fades/volume only on selected audio clips; volume excludes edges and
 *  fades; lower half excludes edges). */
ClipHit clipHit(int x, int y, const ClipSnapshot& s);

/** Raw handle hit tests, ungated by selection (audio-only still applies).
 *  Gesture code that checks selection itself uses these; clipHit() applies
 *  the selected gate on top (#1721). */
bool clipFadeInHandleHit(int x, int y, const ClipSnapshot& s);
bool clipFadeOutHandleHit(int x, int y, const ClipSnapshot& s);
bool clipVolumeLineHit(int y, const ClipSnapshot& s);

/** Cursor for a clip hit. Modifier tools take priority (Shift+Ctrl erase,
 *  Alt copy-drag, Cmd+Alt blade), then the zone table. `selected` gates the
 *  handle/resize/grab cursors exactly like the historical updateCursor. */
CursorKind clipCursor(const ClipHit& hit, bool selected, const ModifierSnapshot& mods);

// ============================================================================
// App-side mapping
// ============================================================================

/** Map a CursorKind to a real cursor. Defined in ArrangementCursorMap.cpp
 *  (app target only — uses CursorManager, which tests don't link). */
juce::MouseCursor toJuceCursor(CursorKind kind);

}  // namespace magda::interaction
