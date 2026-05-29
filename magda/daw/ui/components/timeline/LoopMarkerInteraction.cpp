#include "LoopMarkerInteraction.hpp"

#include <cmath>

namespace magda {

void LoopMarkerInteraction::setHost(Host host) {
    host_ = std::move(host);
}

void LoopMarkerInteraction::setLoopRange(double start, double end, bool enabled) {
    startPosition_ = start;
    endPosition_ = end;
    enabled_ = enabled;
}

bool LoopMarkerInteraction::mouseDown(int x, int y) {
    if (startPosition_ < 0 || endPosition_ <= startPosition_)
        return false;

    bool isStart = false;
    if (isOnMarker(x, y, isStart)) {
        draggingStart_ = isStart;
        draggingEnd_ = !isStart;
        return true;
    }

    if (isOnTopBorder(x, y)) {
        draggingRegion_ = true;
        double clickPosition = host_.pixelToPosition(x);
        dragOffset_ = clickPosition - startPosition_;
        return true;
    }

    return false;
}

bool LoopMarkerInteraction::mouseDrag(int x, int /*y*/) {
    if (draggingStart_ || draggingEnd_) {
        double newPosition = host_.pixelToPosition(x);
        newPosition = juce::jlimit(0.0, host_.maxPosition, newPosition);
        newPosition = applySnap(newPosition);

        if (draggingStart_) {
            startPosition_ = juce::jmin(newPosition, endPosition_ - 0.01);
        } else {
            endPosition_ = juce::jmax(newPosition, startPosition_ + 0.01);
        }

        if (host_.onLoopChanged)
            host_.onLoopChanged(startPosition_, endPosition_);
        if (host_.onRepaint)
            host_.onRepaint();
        return true;
    }

    if (draggingRegion_) {
        double loopDuration = endPosition_ - startPosition_;
        double clickPosition = host_.pixelToPosition(x);
        double newStart = clickPosition - dragOffset_;
        newStart = applySnap(newStart);
        newStart = juce::jmax(0.0, newStart);
        double newEnd = newStart + loopDuration;

        if (newEnd > host_.maxPosition) {
            newEnd = host_.maxPosition;
            newStart = newEnd - loopDuration;
        }

        startPosition_ = newStart;
        endPosition_ = newEnd;

        if (host_.onLoopChanged)
            host_.onLoopChanged(startPosition_, endPosition_);
        if (host_.onRepaint)
            host_.onRepaint();
        return true;
    }

    return false;
}

bool LoopMarkerInteraction::mouseUp(int /*x*/, int /*y*/) {
    bool wasDragging = draggingStart_ || draggingEnd_ || draggingRegion_;
    draggingStart_ = false;
    draggingEnd_ = false;
    draggingRegion_ = false;
    return wasDragging;
}

juce::MouseCursor LoopMarkerInteraction::getCursor(int x, int y) const {
    if (startPosition_ < 0 || endPosition_ <= startPosition_)
        return {};

    bool isStart = false;
    if (isOnMarker(x, y, isStart))
        return juce::MouseCursor::LeftRightResizeCursor;

    if (isOnTopBorder(x, y))
        return juce::MouseCursor::DraggingHandCursor;

    return {};
}

bool LoopMarkerInteraction::isDragging() const {
    return draggingStart_ || draggingEnd_ || draggingRegion_;
}

bool LoopMarkerInteraction::isOnMarker(int x, bool& isStart) const {
    return isOnMarker(x, -1, isStart);
}

bool LoopMarkerInteraction::isOnMarker(int x, int y, bool& isStart) const {
    if (!host_.positionToPixel)
        return false;

    // Only respond within the strip and tick area (from topBorderY downward)
    if (y >= 0 && y < host_.topBorderY)
        return false;

    int startX = host_.positionToPixel(startPosition_);
    int endX = host_.positionToPixel(endPosition_);

    if (std::abs(x - startX) <= HIT_THRESHOLD) {
        isStart = true;
        return true;
    }
    if (std::abs(x - endX) <= HIT_THRESHOLD) {
        isStart = false;
        return true;
    }
    return false;
}

bool LoopMarkerInteraction::isOnTopBorder(int x, int y) const {
    if (!host_.positionToPixel)
        return false;

    // Asymmetric hit zone around the strip's top edge: clicks just slightly
    // above the strip count (so the line itself is easy to grab), but the
    // larger draggable area extends DOWN through the strip body, not up. A
    // symmetric threshold here captured clicks well above the visible strip
    // and silently turned what looked like a ruler-zoom drag into a loop
    // region drag — the cursor still showed the zoom lens because the
    // caller's mouse-move only consults the loop cursor inside the strip.
    constexpr int aboveTolerance = 4;
    if (y < host_.topBorderY - aboveTolerance || y > host_.topBorderY + host_.topBorderThreshold)
        return false;

    int startX = host_.positionToPixel(startPosition_);
    int endX = host_.positionToPixel(endPosition_);

    return x > (startX + REGION_HORIZONTAL_MARGIN) && x < (endX - REGION_HORIZONTAL_MARGIN);
}

double LoopMarkerInteraction::applySnap(double position) const {
    if (host_.snapPosition)
        return host_.snapPosition(position);
    return position;
}

}  // namespace magda
