// Tests for the arrangement pointer hit tester (#1718, #1719): the pure zone
// model shared by the cursor chooser and (eventually) mouseDown dispatch.
// Everything here runs headless on plain-data snapshots.

#include <catch2/catch_test_macros.hpp>

#include "magda/daw/ui/interaction/ArrangementHitTester.hpp"

using namespace magda::interaction;

namespace {

// A panel with two 100px-tall lanes (hit band == lane area, no automation
// lanes), 800px wide, 1px-per-beat zoom, no left padding: beat == pixel.
PanelSnapshot makePanel() {
    PanelSnapshot s;
    s.pixelsPerBeat = 1.0;
    s.leftPadding = 0;
    s.lanes.push_back({0, 100, juce::Rectangle<int>(0, 0, 800, 100)});
    s.lanes.push_back({100, 100, juce::Rectangle<int>(0, 100, 800, 100)});
    return s;
}

PanelSnapshot makePanelWithSelection(double startBeats, double endBeats,
                                     const std::set<int>* tracks = nullptr) {
    auto s = makePanel();
    s.selectionActive = true;
    s.selStartBeats = startBeats;
    s.selEndBeats = endBeats;
    s.selectedTracks = tracks;
    return s;
}

// A 200x60 clip; waveform area is x:[2,198), y:[18,42) (header 16 + 2px
// inset). 2s long => 98 px/s. Selected audio unless overridden.
ClipSnapshot makeAudioClip() {
    ClipSnapshot s;
    s.width = 200;
    s.height = 60;
    s.selected = true;
    s.isAudio = true;
    s.clipLengthSeconds = 2.0;
    s.fadeInSeconds = 0.5;     // handle at x = 2 + 0.5 * 98 = 51
    s.fadeOutSeconds = 0.0;    // handle at x = 198
    s.volumeGainLinear = 0.5;  // line at y = 18 + 0.5 * 24 = 30
    return s;
}

constexpr ModifierSnapshot NO_MODS{};
constexpr ModifierSnapshot SHIFT{true, false, false, false};

}  // namespace

// ============================================================================
// Panel surface
// ============================================================================

TEST_CASE("Panel: empty lane zones split at the lane midline", "[hit-tester]") {
    const auto s = makePanel();

    REQUIRE(panelZone(400, 20, s) == PanelZone::EmptyLaneUpper);
    REQUIRE(panelZone(400, 80, s) == PanelZone::EmptyLaneLower);
    // Second lane has its own midline.
    REQUIRE(panelZone(400, 120, s) == PanelZone::EmptyLaneUpper);
    REQUIRE(panelZone(400, 180, s) == PanelZone::EmptyLaneLower);

    // The hover cursor is modifier-independent: the pen (NoteDraw) only shows
    // once a Shift-draw actually begins, so Shift stays free for Shift+wheel
    // horizontal scroll instead of flashing the pen on hover.
    REQUIRE(panelCursor(PanelZone::EmptyLaneUpper) == CursorKind::Crosshair);
    REQUIRE(panelCursor(PanelZone::EmptyLaneLower) == CursorKind::IBeam);
}

TEST_CASE("Panel: outside every lane is None / Normal", "[hit-tester]") {
    const auto s = makePanel();
    REQUIRE(panelZone(400, 250, s) == PanelZone::None);
    REQUIRE(panelCursor(PanelZone::None) == CursorKind::Normal);
}

TEST_CASE("Panel: a clip owns its point when no selection is active", "[hit-tester]") {
    auto s = makePanel();
    s.clipAtPoint = true;
    REQUIRE(panelZone(400, 20, s) == PanelZone::OverClip);
    REQUIRE(panelCursor(PanelZone::OverClip) == CursorKind::Normal);
}

TEST_CASE("Panel: selection body beats everything, including clips", "[hit-tester]") {
    auto s = makePanelWithSelection(100.0, 300.0);
    s.clipAtPoint = true;

    REQUIRE(panelZone(200, 20, s) == PanelZone::SelectionBody);
    REQUIRE(panelCursor(PanelZone::SelectionBody) == CursorKind::DraggingHand);
}

TEST_CASE("Panel: selection edges win within the threshold, left first", "[hit-tester]") {
    const auto s = makePanelWithSelection(100.0, 300.0);

    REQUIRE(panelZone(100, 20, s) == PanelZone::SelectionEdgeLeft);
    REQUIRE(panelZone(100 + SELECTION_EDGE_THRESHOLD, 20, s) == PanelZone::SelectionEdgeLeft);
    REQUIRE(panelZone(300 - SELECTION_EDGE_THRESHOLD, 20, s) == PanelZone::SelectionEdgeRight);
    REQUIRE(panelZone(300 + SELECTION_EDGE_THRESHOLD, 20, s) == PanelZone::SelectionEdgeRight);
    // One past the threshold: interior is body, exterior falls through.
    REQUIRE(panelZone(100 + SELECTION_EDGE_THRESHOLD + 1, 20, s) == PanelZone::SelectionBody);
    REQUIRE(panelZone(300 + SELECTION_EDGE_THRESHOLD + 1, 20, s) == PanelZone::EmptyLaneUpper);

    // A degenerate selection: both boundaries within threshold of x — the
    // left edge is checked first (historical isOnSelectionEdge order).
    const auto tiny = makePanelWithSelection(100.0, 104.0);
    REQUIRE(panelZone(102, 20, tiny) == PanelZone::SelectionEdgeLeft);

    REQUIRE(panelCursor(PanelZone::SelectionEdgeLeft) == CursorKind::LeftRightResize);
    REQUIRE(panelCursor(PanelZone::SelectionEdgeRight) == CursorKind::LeftRightResize);
}

TEST_CASE("Panel: per-track selections only hit their tracks", "[hit-tester]") {
    const std::set<int> secondLaneOnly{1};
    const auto s = makePanelWithSelection(100.0, 300.0, &secondLaneOnly);

    // Lane 0 is not part of the selection: falls through to lane zones.
    REQUIRE(panelZone(200, 20, s) == PanelZone::EmptyLaneUpper);
    REQUIRE(panelZone(100, 20, s) == PanelZone::EmptyLaneUpper);
    // Lane 1 is.
    REQUIRE(panelZone(200, 120, s) == PanelZone::SelectionBody);
    REQUIRE(panelZone(100, 120, s) == PanelZone::SelectionEdgeLeft);

    // Empty track set = all tracks (SelectionState::includesTrack semantics).
    const std::set<int> empty;
    const auto all = makePanelWithSelection(100.0, 300.0, &empty);
    REQUIRE(panelZone(200, 20, all) == PanelZone::SelectionBody);
}

TEST_CASE("Panel: panelHit exposes the raw facts gesture code branches on", "[hit-tester]") {
    const auto s = makePanelWithSelection(100.0, 300.0);

    // A point just inside the left boundary is BOTH on the edge (zone) and
    // inside the selection rectangle — gesture code needs the raw fact, the
    // cursor needs the priority. The old duplicated predicates kept these
    // subtly different; panelHit carries both.
    const auto nearEdgeInside = panelHit(103, 20, s);
    REQUIRE(nearEdgeInside.zone == PanelZone::SelectionEdgeLeft);
    REQUIRE(nearEdgeInside.onSelectionEdge);
    REQUIRE(nearEdgeInside.selectionEdgeIsLeft);
    REQUIRE(nearEdgeInside.insideSelection);

    // Just outside the boundary: edge zone, but NOT inside the rectangle.
    const auto nearEdgeOutside = panelHit(97, 20, s);
    REQUIRE(nearEdgeOutside.zone == PanelZone::SelectionEdgeLeft);
    REQUIRE_FALSE(nearEdgeOutside.insideSelection);

    const auto body = panelHit(200, 20, s);
    REQUIRE(body.laneIndex == 0);
    REQUIRE(body.inUpperZone);
    REQUIRE(body.selectable);
    REQUIRE_FALSE(body.onSelectionEdge);
    REQUIRE(body.insideSelection);

    const auto lowerLane1 = panelHit(400, 180, makePanel());
    REQUIRE(lowerLane1.laneIndex == 1);
    REQUIRE_FALSE(lowerLane1.inUpperZone);

    const auto outside = panelHit(400, 250, makePanel());
    REQUIRE(outside.laneIndex == -1);
    REQUIRE_FALSE(outside.selectable);
}

TEST_CASE("Panel: selection needs a lane hit band, automation gaps miss", "[hit-tester]") {
    // Lane hit band is the track's own height only; y below it (e.g. an
    // automation lane) must not report the selection.
    PanelSnapshot s;
    s.pixelsPerBeat = 1.0;
    s.leftPadding = 0;
    // 60px hit band inside a 100px lane area (40px of automation below).
    s.lanes.push_back({0, 60, juce::Rectangle<int>(0, 0, 800, 100)});
    s.selectionActive = true;
    s.selStartBeats = 100.0;
    s.selEndBeats = 300.0;

    REQUIRE(panelZone(200, 30, s) == PanelZone::SelectionBody);
    REQUIRE(laneIndexAtY(80, s) == -1);
    REQUIRE(panelZone(200, 80, s) != PanelZone::SelectionBody);
}

// ============================================================================
// Clip surface
// ============================================================================

TEST_CASE("Clip: resize edges and the lower-half band", "[hit-tester]") {
    const auto s = makeAudioClip();

    REQUIRE(clipHit(3, 10, s).zone == ClipZone::EdgeLeft);
    REQUIRE(clipHit(197, 10, s).zone == ClipZone::EdgeRight);

    // Lower half away from the edges is the time-selection band...
    const auto lower = clipHit(100, 45, s);
    REQUIRE(lower.zone == ClipZone::LowerHalf);
    REQUIRE(lower.lowerHalf);
    // ...but the edges keep priority over it.
    const auto lowerEdge = clipHit(3, 45, s);
    REQUIRE_FALSE(lowerEdge.lowerHalf);
    REQUIRE(lowerEdge.zone == ClipZone::EdgeLeft);

    // Edge cursors: resize when selected, stretch with Shift, nothing when
    // unselected (click to select first).
    REQUIRE(clipCursor(lowerEdge, true, NO_MODS) == CursorKind::LeftRightResize);
    REQUIRE(clipCursor(lowerEdge, true, SHIFT) == CursorKind::UpDownLeftRightResize);
    REQUIRE(clipCursor(lowerEdge, false, NO_MODS) == CursorKind::Normal);
    // The lower band is an I-beam regardless of selection.
    REQUIRE(clipCursor(lower, true, NO_MODS) == CursorKind::IBeam);
    REQUIRE(clipCursor(lower, false, NO_MODS) == CursorKind::IBeam);
}

TEST_CASE("Clip: body is grab-when-selected, inert otherwise", "[hit-tester]") {
    const auto s = makeAudioClip();
    const auto body = clipHit(100, 10, s);
    REQUIRE(body.zone == ClipZone::Body);

    REQUIRE(clipCursor(body, true, NO_MODS) == CursorKind::DraggingHand);
    REQUIRE(clipCursor(body, false, NO_MODS) == CursorKind::Normal);
}

TEST_CASE("Clip: fade handles exist on selected audio clips only", "[hit-tester]") {
    auto s = makeAudioClip();

    // Fade-in handle at x=51, in the top band of the waveform area.
    const auto onHandle = clipHit(51, 20, s);
    REQUIRE(onHandle.zone == ClipZone::FadeIn);
    REQUIRE(clipCursor(onHandle, true, NO_MODS) == CursorKind::PointingHand);

    // Fade-out handle at the waveform right edge; it also overlaps the
    // resize edge — the fade handle wins (historical cursor priority).
    const auto onFadeOut = clipHit(197, 20, s);
    REQUIRE(onFadeOut.onFadeOut);
    REQUIRE(onFadeOut.onRightEdge);
    REQUIRE(onFadeOut.zone == ClipZone::FadeOut);

    // Below the top band, or away from the handle: no fade zone.
    REQUIRE_FALSE(clipHit(51, 40, s).onFadeIn);
    REQUIRE_FALSE(clipHit(100, 20, s).onFadeIn);

    // Unselected or MIDI clips have no fade handles.
    s.selected = false;
    REQUIRE_FALSE(clipHit(51, 20, s).onFadeIn);
    s.selected = true;
    s.isAudio = false;
    REQUIRE_FALSE(clipHit(51, 20, s).onFadeIn);
}

TEST_CASE("Clip: volume line yields to edges and fade handles", "[hit-tester]") {
    const auto s = makeAudioClip();

    // Volume line at y=30, away from the fade handles.
    const auto onLine = clipHit(120, 30, s);
    REQUIRE(onLine.onVolume);
    REQUIRE(onLine.zone == ClipZone::Volume);
    REQUIRE(clipCursor(onLine, true, NO_MODS) == CursorKind::UpDownResize);

    // Same y on the resize edge: the edge excludes the volume handle.
    const auto onEdge = clipHit(3, 30, s);
    REQUIRE_FALSE(onEdge.onVolume);
    // Near the fade-in handle x at a fade-band y: fade wins.
    const auto nearFade = clipHit(51, 30, s);
    REQUIRE(nearFade.onFadeIn);
    REQUIRE_FALSE(nearFade.onVolume);
}

TEST_CASE("Clip: raw handle hits ignore selection but respect audio", "[hit-tester]") {
    auto s = makeAudioClip();
    s.selected = false;

    // Gesture code gates on selection itself, so the raw predicates hit
    // even on unselected clips (unlike clipHit's gated flags)...
    REQUIRE(clipFadeInHandleHit(51, 20, s));
    REQUIRE(clipFadeOutHandleHit(197, 20, s));
    REQUIRE(clipVolumeLineHit(30, s));
    REQUIRE_FALSE(clipHit(51, 20, s).onFadeIn);

    // ...but never on MIDI clips.
    s.isAudio = false;
    REQUIRE_FALSE(clipFadeInHandleHit(51, 20, s));
    REQUIRE_FALSE(clipVolumeLineHit(30, s));
}

TEST_CASE("Clip: modifier tools override every zone", "[hit-tester]") {
    const auto s = makeAudioClip();
    const auto body = clipHit(100, 10, s);

    constexpr ModifierSnapshot ERASE{true, true, false, false};  // Shift+Ctrl
    constexpr ModifierSnapshot COPY{false, false, true, false};  // Alt
    constexpr ModifierSnapshot BLADE{false, false, true, true};  // Cmd+Alt
    constexpr ModifierSnapshot SHIFT_ALT{true, false, true, false};

    REQUIRE(clipCursor(body, true, ERASE) == CursorKind::Erase);
    REQUIRE(clipCursor(body, false, ERASE) == CursorKind::Erase);
    REQUIRE(clipCursor(body, true, COPY) == CursorKind::Copying);
    REQUIRE(clipCursor(body, true, BLADE) == CursorKind::Blade);
    // Shift+Alt = ghost-copy drag (link cursor).
    REQUIRE(clipCursor(body, true, SHIFT_ALT) == CursorKind::GhostCopy);
    REQUIRE(clipCursor(body, false, SHIFT_ALT) == CursorKind::GhostCopy);

    // Tools also override the handles.
    const auto onHandle = clipHit(51, 20, s);
    REQUIRE(clipCursor(onHandle, true, ERASE) == CursorKind::Erase);
}
