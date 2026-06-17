#include "SongNavigatorPanel.hpp"

#include "../../../core/ClipInfo.hpp"
#include "../../layout/LayoutConfig.hpp"
#include "../../themes/DarkTheme.hpp"

namespace magda {

SongNavigatorPanel::SongNavigatorPanel() {
    setOpaque(true);
    TrackManager::getInstance().addListener(this);
    ClipManager::getInstance().addListener(this);
}

SongNavigatorPanel::~SongNavigatorPanel() {
    TrackManager::getInstance().removeListener(this);
    ClipManager::getInstance().removeListener(this);
    if (controller_) {
        controller_->removeListener(this);
    }
}

void SongNavigatorPanel::setController(TimelineController* controller) {
    if (controller_) {
        controller_->removeListener(this);
    }
    controller_ = controller;
    if (controller_) {
        controller_->addListener(this);
    }
    repaint();
}

// ===== Coordinate helpers =====

double SongNavigatorPanel::totalBeats() const {
    if (!controller_) {
        return 1.0;
    }
    // The strip spans the whole project timeline (not just the song content).
    // The mini ruler makes the scale self-evident, so the song's clips end
    // partway with the remaining project length shown empty after them.
    return juce::jmax(1.0, controller_->getState().timelineLengthBeats);
}

int SongNavigatorPanel::beatToX(double beat) const {
    // Left padding matches the main track content; the right gutter matches the
    // arrangement's min-zoom label gutter so the strip's end lines up with the
    // end of the arrangement's bars.
    const int usableWidth =
        juce::jmax(1, getWidth() - LayoutConfig::TIMELINE_LEFT_PADDING -
                          static_cast<int>(TimelineState::MIN_ZOOM_RIGHT_LABEL_GUTTER));
    const double frac = juce::jlimit(0.0, 1.0, beat / totalBeats());
    return LayoutConfig::TIMELINE_LEFT_PADDING + static_cast<int>(std::round(frac * usableWidth));
}

double SongNavigatorPanel::xToBeat(int x) const {
    const int usableWidth =
        juce::jmax(1, getWidth() - LayoutConfig::TIMELINE_LEFT_PADDING -
                          static_cast<int>(TimelineState::MIN_ZOOM_RIGHT_LABEL_GUTTER));
    const double frac = juce::jlimit(
        0.0, 1.0, static_cast<double>(x - LayoutConfig::TIMELINE_LEFT_PADDING) / usableWidth);
    return frac * totalBeats();
}

double SongNavigatorPanel::visibleStartBeats() const {
    if (!controller_) {
        return 0.0;
    }
    const auto& zoom = controller_->getState().zoom;
    if (zoom.horizontalZoom <= 0.0) {
        return 0.0;
    }
    return juce::jmax(0.0,
                      (zoom.scrollX - LayoutConfig::TIMELINE_LEFT_PADDING) / zoom.horizontalZoom);
}

double SongNavigatorPanel::visibleLengthBeats() const {
    if (!controller_) {
        return totalBeats();
    }
    const auto& zoom = controller_->getState().zoom;
    if (zoom.horizontalZoom <= 0.0) {
        return totalBeats();
    }
    return zoom.viewportWidth / zoom.horizontalZoom;
}

juce::Rectangle<float> SongNavigatorPanel::getViewportBox() const {
    const double startBeats = visibleStartBeats();
    const double endBeats = juce::jmin(totalBeats(), startBeats + visibleLengthBeats());
    const int left = beatToX(startBeats);
    const int right = beatToX(endBeats);
    // Span the lanes area only - start below the ruler band, like the playhead.
    return juce::Rectangle<float>(static_cast<float>(left), static_cast<float>(kRulerHeight),
                                  static_cast<float>(juce::jmax(2, right - left)),
                                  static_cast<float>(getHeight() - kRulerHeight));
}

// ===== Painting =====

void SongNavigatorPanel::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::TRACK_BACKGROUND));

    auto bounds = getLocalBounds();

    auto& trackManager = TrackManager::getInstance();
    auto& clipManager = ClipManager::getInstance();
    const auto& tracks = trackManager.getTracks();
    const int numTracks = static_cast<int>(tracks.size());

    // Mini ruler band along the top, plus faint bar gridlines down the strip so
    // the navigator carries its own scale (no need to read it against the main
    // ruler above, which has a different zoom).
    const int beatsPerBar =
        controller_ ? juce::jmax(1, controller_->getState().tempo.timeSignatureNumerator) : 4;
    const double totalBars = totalBeats() / beatsPerBar;
    {
        // Pick a "nice" bar step so labels stay readable (~one per 70px).
        const int maxLabels = juce::jmax(1, getWidth() / 70);
        const double rawStep = totalBars / maxLabels;
        const int niceSteps[] = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512};
        int barStep = niceSteps[0];
        for (int s : niceSteps) {
            barStep = s;
            if (s >= rawStep) {
                break;
            }
        }

        g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.5f));
        g.fillRect(0, 0, getWidth(), kRulerHeight);
        g.setFont(8.0f);

        auto drawTick = [&](int barNumber, double beatPos) {
            const int x = beatToX(beatPos);
            if (x >= getWidth() - 2) {
                return;
            }
            g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.6f));
            g.drawVerticalLine(x, static_cast<float>(kRulerHeight),
                               static_cast<float>(getHeight()));
            g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY).withAlpha(0.7f));
            g.drawText(juce::String(barNumber), x + 2, 0, 40, kRulerHeight,
                       juce::Justification::centredLeft);
        };

        // The end of the project is bar (totalBars + 1) at the content's right
        // edge (just before the gutter).
        const int endBar = static_cast<int>(totalBars) + 1;
        for (int bar = 1; bar < endBar; bar += barStep) {
            // Don't crowd the final boundary label drawn below.
            if (endBar - bar < barStep) {
                continue;
            }
            drawTick(bar, (bar - 1) * static_cast<double>(beatsPerBar));
        }
        drawTick(endBar, totalBars * static_cast<double>(beatsPerBar));
    }

    // Height is limited, so merge tracks onto a few lanes rather than one thin
    // lane per track. numLanes = numTracks when they fit, else fewer (each lane
    // then holds several tracks' clips stacked together).
    bool hasContent = false;
    if (numTracks > 0) {
        const int lanesTop = kRulerHeight + kVInset;
        const int availableHeight = juce::jmax(1, getHeight() - lanesTop - kVInset);
        const int minLaneHeight = 3;
        const int maxLanes = juce::jmax(1, availableHeight / minLaneHeight);
        const int numLanes = juce::jmin(numTracks, maxLanes);
        const float laneHeight = static_cast<float>(availableHeight) / static_cast<float>(numLanes);

        for (int i = 0; i < numTracks; ++i) {
            const auto& track = tracks[i];
            const int laneIndex = (i * numLanes) / numTracks;
            const float laneY = static_cast<float>(lanesTop) + laneIndex * laneHeight;

            const auto clipIds = clipManager.getClipsOnTrack(track.id, ClipView::Arrangement);
            for (auto clipId : clipIds) {
                const auto* clip = clipManager.getClip(clipId);
                if (clip == nullptr) {
                    continue;
                }

                const double startBeat = clip->placement.startBeat;
                const double endBeat = startBeat + clip->placement.lengthBeats;
                const int x = beatToX(startBeat);
                const int w = juce::jmax(1, beatToX(endBeat) - x);

                juce::Colour colour = clip->colour;
                if (colour.isTransparent()) {
                    colour = track.colour;
                }

                juce::Rectangle<float> clipRect(static_cast<float>(x), laneY + 0.5f,
                                                static_cast<float>(w),
                                                juce::jmax(1.0f, laneHeight - 1.0f));
                g.setColour(colour.withAlpha(0.85f));
                g.fillRect(clipRect);
                hasContent = true;
            }
        }
    }

    // Timeline markers - a coloured line down the strip with a small flag tick
    // in the ruler band, mirroring the arrangement's markers.
    if (controller_) {
        for (const auto& marker : controller_->getState().markers) {
            const int mx = beatToX(marker.positionBeats);
            if (mx >= getWidth() - 1) {
                continue;
            }
            g.setColour(marker.colour.withAlpha(0.85f));
            g.drawVerticalLine(mx, static_cast<float>(kRulerHeight),
                               static_cast<float>(getHeight()));
            // Flag tick at the top of the content area (not in the ruler header).
            g.fillRect(mx, kRulerHeight, 4, 4);
        }
    }

    // Playhead marker - starts below the ruler band, like the gridlines.
    if (controller_) {
        const double playBeats = controller_->getState().playhead.playbackPositionBeats;
        const int px = beatToX(playBeats);
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE).withAlpha(0.9f));
        g.drawVerticalLine(px, static_cast<float>(kRulerHeight), static_cast<float>(getHeight()));
    }

    // Viewport rectangle marking the currently-visible window. Hidden when the
    // project is empty - a big selection rectangle over a blank strip is noise.
    if (hasContent) {
        const auto box = getViewportBox();
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_CYAN).withAlpha(0.15f));
        g.fillRect(box);
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_CYAN).withAlpha(0.9f));
        g.drawRect(box, 1.5f);
    }

    // Outer border.
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRect(bounds, 1);
}

// ===== Interaction =====

SongNavigatorPanel::DragMode SongNavigatorPanel::viewportHitTest(juce::Point<int> pos) const {
    const auto box = getViewportBox();
    if (std::abs(pos.x - box.getX()) <= kEdgeGrabPx) {
        return DragMode::ResizeLeft;
    }
    if (std::abs(pos.x - box.getRight()) <= kEdgeGrabPx) {
        return DragMode::ResizeRight;
    }
    if (box.contains(static_cast<float>(pos.x), static_cast<float>(pos.y))) {
        return DragMode::MoveViewport;
    }
    return DragMode::None;
}

void SongNavigatorPanel::mouseMove(const juce::MouseEvent& event) {
    switch (viewportHitTest(event.getPosition())) {
        case DragMode::ResizeLeft:
        case DragMode::ResizeRight:
            setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
            break;
        case DragMode::MoveViewport:
            setMouseCursor(juce::MouseCursor::DraggingHandCursor);
            break;
        case DragMode::None:
        default:
            setMouseCursor(juce::MouseCursor::NormalCursor);
            break;
    }
}

void SongNavigatorPanel::mouseDown(const juce::MouseEvent& event) {
    if (!controller_) {
        return;
    }

    dragMode_ = viewportHitTest(event.getPosition());

    if (dragMode_ == DragMode::None) {
        // Click on empty strip: recentre the arrangement on the clicked beat.
        controller_->dispatch(ScrollToBeatEvent{xToBeat(event.x), true});
        dragMode_ = DragMode::MoveViewport;
        dragGrabOffsetBeats_ = 0.0;
        return;
    }

    if (dragMode_ == DragMode::MoveViewport) {
        dragGrabOffsetBeats_ = xToBeat(event.x) - visibleStartBeats();
    }
}

void SongNavigatorPanel::mouseDrag(const juce::MouseEvent& event) {
    if (!controller_ || dragMode_ == DragMode::None) {
        return;
    }

    const double mouseBeat = xToBeat(event.x);

    switch (dragMode_) {
        case DragMode::MoveViewport:
            panToStartBeat(mouseBeat - dragGrabOffsetBeats_);
            break;
        case DragMode::ResizeLeft: {
            const double endBeats = visibleStartBeats() + visibleLengthBeats();
            const double newStart = juce::jlimit(0.0, endBeats - 0.5, mouseBeat);
            zoomToVisibleRange(newStart, endBeats - newStart);
            break;
        }
        case DragMode::ResizeRight: {
            const double startBeats = visibleStartBeats();
            const double newEnd = juce::jmax(startBeats + 0.5, mouseBeat);
            zoomToVisibleRange(startBeats, newEnd - startBeats);
            break;
        }
        case DragMode::None:
        default:
            break;
    }
}

void SongNavigatorPanel::mouseUp(const juce::MouseEvent& /*event*/) {
    dragMode_ = DragMode::None;
}

void SongNavigatorPanel::panToStartBeat(double startBeats) {
    if (!controller_) {
        return;
    }
    const auto& zoom = controller_->getState().zoom;
    const double clampedStart = juce::jmax(0.0, startBeats);
    const int scrollX = static_cast<int>(std::round(clampedStart * zoom.horizontalZoom)) +
                        LayoutConfig::TIMELINE_LEFT_PADDING;
    controller_->dispatch(SetScrollPositionEvent{scrollX, -1});
}

void SongNavigatorPanel::zoomToVisibleRange(double startBeats, double lengthBeats) {
    if (!controller_ || lengthBeats <= 0.0) {
        return;
    }
    const auto& zoom = controller_->getState().zoom;
    if (zoom.viewportWidth <= 0) {
        return;
    }

    const double newZoom = static_cast<double>(zoom.viewportWidth) / lengthBeats;
    // Anchor the window start at the left edge of the viewport (screen X 0) so
    // the dragged edge follows the cursor while the opposite edge holds.
    controller_->dispatch(SetZoomAnchoredBeatsEvent{newZoom, juce::jmax(0.0, startBeats), 0});
}

// ===== Listeners =====

void SongNavigatorPanel::timelineStateChanged(const TimelineState& /*state*/,
                                              ChangeFlags /*changes*/) {
    // The whole strip is derived from timeline state (zoom, scroll, playhead,
    // length, tempo, markers), so just repaint when the state changes rather
    // than filtering on individual flags.
    repaint();
}

void SongNavigatorPanel::tracksChanged() {
    repaint();
}

void SongNavigatorPanel::clipsChanged() {
    repaint();
}

void SongNavigatorPanel::clipPropertyChanged(ClipId /*clipId*/) {
    repaint();
}

void SongNavigatorPanel::clipPropertiesChanged(const std::vector<ClipId>& /*clipIds*/) {
    repaint();
}

}  // namespace magda
