#include "ArrangementHitTester.hpp"

#include <cmath>

namespace magda::interaction {

// ============================================================================
// Panel surface
// ============================================================================

namespace {

// TrackContentPanel::pixelToBeats — <= 0 zoom degrades to beat 0.
double pixelToBeats(int pixel, const PanelSnapshot& s) {
    if (s.pixelsPerBeat > 0)
        return static_cast<double>(pixel - s.leftPadding) / s.pixelsPerBeat;
    return 0.0;
}

// TrackContentPanel::beatsToPixel.
int beatsToPixel(double beats, const PanelSnapshot& s) {
    return static_cast<int>(std::round(beats * s.pixelsPerBeat)) + s.leftPadding;
}

bool selectionIncludesLane(int laneIndex, const PanelSnapshot& s) {
    // SelectionState::includesTrack: empty set means all tracks.
    return s.selectedTracks == nullptr || s.selectedTracks->empty() ||
           s.selectedTracks->count(laneIndex) > 0;
}

// TrackContentPanel::isInSelectableArea — inside any lane rectangle.
bool inSelectableArea(int x, int y, const PanelSnapshot& s) {
    for (const auto& lane : s.lanes)
        if (lane.area.contains(x, y))
            return true;
    return false;
}

// TrackContentPanel::isOnSelectionEdge — near a selection boundary on a
// selected track. Left edge wins ties, matching the historical order.
bool onSelectionEdge(int x, int y, const PanelSnapshot& s, bool& isLeftEdge) {
    if (!s.selectionActive)
        return false;

    const int laneIndex = laneIndexAtY(y, s);
    if (laneIndex < 0 || !selectionIncludesLane(laneIndex, s))
        return false;

    const int startX = beatsToPixel(s.selStartBeats, s);
    const int endX = beatsToPixel(s.selEndBeats, s);

    if (std::abs(x - startX) <= SELECTION_EDGE_THRESHOLD) {
        isLeftEdge = true;
        return true;
    }
    if (std::abs(x - endX) <= SELECTION_EDGE_THRESHOLD) {
        isLeftEdge = false;
        return true;
    }
    return false;
}

// TrackContentPanel::isOnExistingSelection — inside the selection's beat
// range on a selected track.
bool onExistingSelection(int x, int y, const PanelSnapshot& s) {
    if (!s.selectionActive)
        return false;

    const double clickBeats = pixelToBeats(x, s);
    if (clickBeats < s.selStartBeats || clickBeats > s.selEndBeats)
        return false;

    const int laneIndex = laneIndexAtY(y, s);
    return laneIndex >= 0 && selectionIncludesLane(laneIndex, s);
}

}  // namespace

int laneIndexAtY(int y, const PanelSnapshot& s) {
    for (size_t i = 0; i < s.lanes.size(); ++i) {
        const auto& lane = s.lanes[i];
        if (y >= lane.hitTop && y < lane.hitTop + lane.hitHeight)
            return static_cast<int>(i);
    }
    return -1;  // not in any track's own band (or in an automation lane)
}

PanelHit panelHit(int x, int y, const PanelSnapshot& s) {
    PanelHit hit;
    hit.laneIndex = laneIndexAtY(y, s);
    // Upper half of a lane = clip/marquee zone, lower half (and everything
    // below the last hit band, e.g. automation lanes) = time-selection zone.
    hit.inUpperZone =
        hit.laneIndex >= 0 && y < s.lanes[static_cast<size_t>(hit.laneIndex)].area.getCentreY();
    hit.selectable = inSelectableArea(x, y, s);
    hit.onSelectionEdge = onSelectionEdge(x, y, s, hit.selectionEdgeIsLeft);
    hit.insideSelection = onExistingSelection(x, y, s);

    // Priority mirrors the panel's historical cursor logic: an active time
    // selection wins over everything (clips are hit-transparent under it),
    // then clip passthrough, then the lane zones.
    if (hit.onSelectionEdge)
        hit.zone =
            hit.selectionEdgeIsLeft ? PanelZone::SelectionEdgeLeft : PanelZone::SelectionEdgeRight;
    else if (hit.insideSelection)
        hit.zone = PanelZone::SelectionBody;
    else if (s.clipAtPoint)
        hit.zone = PanelZone::OverClip;
    else if (!hit.selectable)
        hit.zone = PanelZone::None;
    else
        hit.zone = hit.inUpperZone ? PanelZone::EmptyLaneUpper : PanelZone::EmptyLaneLower;
    return hit;
}

PanelZone panelZone(int x, int y, const PanelSnapshot& s) {
    return panelHit(x, y, s).zone;
}

CursorKind panelCursor(PanelZone zone, bool shiftHeld) {
    switch (zone) {
        case PanelZone::SelectionEdgeLeft:
        case PanelZone::SelectionEdgeRight:
            return CursorKind::LeftRightResize;
        case PanelZone::SelectionBody:
            return CursorKind::DraggingHand;
        case PanelZone::OverClip:
            return CursorKind::Normal;
        case PanelZone::EmptyLaneUpper:
            return shiftHeld ? CursorKind::NoteDraw : CursorKind::Crosshair;
        case PanelZone::EmptyLaneLower:
            return shiftHeld ? CursorKind::NoteDraw : CursorKind::IBeam;
        case PanelZone::None:
            break;
    }
    return CursorKind::Normal;
}

// ============================================================================
// Clip surface
// ============================================================================

namespace {

// ClipComponent's waveform area: getLocalBounds().reduced(2, HEADER_HEIGHT + 2).
juce::Rectangle<int> waveformArea(const ClipSnapshot& s) {
    return juce::Rectangle<int>(0, 0, s.width, s.height).reduced(2, ClipMetrics::HEADER_HEIGHT + 2);
}

// ClipComponent::isOnFadeInHandle / isOnFadeOutHandle.
bool onFadeHandle(int x, int y, const ClipSnapshot& s, bool fadeIn) {
    if (!s.isAudio)
        return false;

    const auto area = waveformArea(s);
    if (area.getWidth() <= 0)
        return false;

    // Handle zone is the top band of the waveform area.
    if (y < area.getY() || y > area.getY() + ClipMetrics::FADE_HANDLE_HIT_WIDTH)
        return false;

    const double pps = (s.clipLengthSeconds > 0.0)
                           ? static_cast<double>(area.getWidth()) / s.clipLengthSeconds
                           : 0.0;
    if (pps <= 0.0)
        return false;

    const float handleX =
        fadeIn ? static_cast<float>(area.getX()) + static_cast<float>(s.fadeInSeconds * pps)
               : static_cast<float>(area.getRight()) - static_cast<float>(s.fadeOutSeconds * pps);
    return std::abs(static_cast<float>(x) - handleX) <=
           static_cast<float>(ClipMetrics::FADE_HANDLE_HIT_WIDTH) * 0.5f;
}

// ClipComponent::isOnVolumeHandle — near the volume line.
bool onVolumeLine(int y, const ClipSnapshot& s) {
    if (!s.isAudio)
        return false;

    const auto area = waveformArea(s);
    if (area.getWidth() <= 0 || area.getHeight() <= 0)
        return false;

    const float gain = juce::jlimit(0.0f, 1.0f, s.volumeGainLinear);
    const float lineY =
        static_cast<float>(area.getY()) + ((1.0f - gain) * static_cast<float>(area.getHeight()));
    return std::abs(static_cast<float>(y) - lineY) <= ClipMetrics::VOLUME_LINE_HIT_DISTANCE;
}

}  // namespace

bool clipFadeInHandleHit(int x, int y, const ClipSnapshot& s) {
    return onFadeHandle(x, y, s, true);
}

bool clipFadeOutHandleHit(int x, int y, const ClipSnapshot& s) {
    return onFadeHandle(x, y, s, false);
}

bool clipVolumeLineHit(int y, const ClipSnapshot& s) {
    return onVolumeLine(y, s);
}

ClipHit clipHit(int x, int y, const ClipSnapshot& s) {
    ClipHit hit;
    hit.onLeftEdge = x < ClipMetrics::RESIZE_HANDLE_WIDTH;
    hit.onRightEdge = x > s.width - ClipMetrics::RESIZE_HANDLE_WIDTH;

    const bool onEdge = hit.onLeftEdge || hit.onRightEdge;

    // Lower half (away from the resize edges) is the time-selection band.
    hit.lowerHalf = y >= s.height / 2 && !onEdge;

    // Fade + volume handles exist only on selected audio clips; the volume
    // line additionally yields to edges and fade handles.
    if (s.selected) {
        hit.onFadeIn = onFadeHandle(x, y, s, true);
        hit.onFadeOut = onFadeHandle(x, y, s, false);
        hit.onVolume = !hit.onFadeIn && !hit.onFadeOut && !onEdge && onVolumeLine(y, s);
    }

    // Winning zone, in the historical cursor-priority order.
    if (hit.onFadeIn)
        hit.zone = ClipZone::FadeIn;
    else if (hit.onFadeOut)
        hit.zone = ClipZone::FadeOut;
    else if (hit.onVolume)
        hit.zone = ClipZone::Volume;
    else if (hit.lowerHalf)
        hit.zone = ClipZone::LowerHalf;
    else if (hit.onLeftEdge)
        hit.zone = ClipZone::EdgeLeft;
    else if (hit.onRightEdge)
        hit.zone = ClipZone::EdgeRight;
    else
        hit.zone = ClipZone::Body;
    return hit;
}

CursorKind clipCursor(const ClipHit& hit, bool selected, const ModifierSnapshot& mods) {
    // Modifier tools first (ClipComponent::updateCursor order).
    if (mods.shift && mods.ctrl)
        return CursorKind::Erase;
    // Cmd+Alt = blade (scissors), Alt alone = copy-drag.
    if (mods.alt && !mods.shift)
        return mods.command ? CursorKind::Blade : CursorKind::Copying;

    if (selected && (hit.onFadeIn || hit.onFadeOut))
        return CursorKind::PointingHand;
    if (selected && hit.onVolume)
        return CursorKind::UpDownResize;

    // Lower half is a time-selection band regardless of selection state.
    if (hit.lowerHalf)
        return CursorKind::IBeam;

    if (selected && (hit.onLeftEdge || hit.onRightEdge))
        return mods.shift ? CursorKind::UpDownLeftRightResize : CursorKind::LeftRightResize;
    if (selected)
        return CursorKind::DraggingHand;  // grab-to-move zone (policy: #1718)
    return CursorKind::Normal;            // unselected: click to select first
}

}  // namespace magda::interaction
