#include "LoopMarkerInteraction.hpp"

#include <cmath>

namespace magda {

void LoopMarkerInteraction::setHost(Host host) {
    host_ = std::move(host);
}

void LoopMarkerInteraction::setLoopRegion(double start, double end, bool enabled) {
    startTime_ = start;
    endTime_ = end;
    enabled_ = enabled;
}

bool LoopMarkerInteraction::mouseDown(int x, int y) {
    if (startTime_ < 0 || endTime_ <= startTime_)
        return false;

    bool isStart = false;
    if (isOnMarker(x, isStart)) {
        draggingStart_ = isStart;
        draggingEnd_ = !isStart;
        return true;
    }

    if (isOnTopBorder(x, y)) {
        draggingRegion_ = true;
        double clickTime = host_.pixelToTime(x);
        dragOffset_ = clickTime - startTime_;
        return true;
    }

    return false;
}

bool LoopMarkerInteraction::mouseDrag(int x, int /*y*/) {
    if (draggingStart_ || draggingEnd_) {
        double newTime = host_.pixelToTime(x);
        newTime = juce::jlimit(0.0, host_.maxTime, newTime);
        newTime = applySnap(newTime);

        if (draggingStart_) {
            startTime_ = juce::jmin(newTime, endTime_ - 0.01);
        } else {
            endTime_ = juce::jmax(newTime, startTime_ + 0.01);
        }

        if (host_.onLoopChanged)
            host_.onLoopChanged(startTime_, endTime_);
        if (host_.onRepaint)
            host_.onRepaint();
        return true;
    }

    if (draggingRegion_) {
        double loopDuration = endTime_ - startTime_;
        double clickTime = host_.pixelToTime(x);
        double newStart = clickTime - dragOffset_;
        newStart = applySnap(newStart);
        newStart = juce::jmax(0.0, newStart);
        double newEnd = newStart + loopDuration;

        if (newEnd > host_.maxTime) {
            newEnd = host_.maxTime;
            newStart = newEnd - loopDuration;
        }

        startTime_ = newStart;
        endTime_ = newEnd;

        if (host_.onLoopChanged)
            host_.onLoopChanged(startTime_, endTime_);
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
    if (startTime_ < 0 || endTime_ <= startTime_)
        return {};

    bool isStart = false;
    if (isOnMarker(x, isStart))
        return juce::MouseCursor::LeftRightResizeCursor;

    if (isOnTopBorder(x, y))
        return juce::MouseCursor::DraggingHandCursor;

    return {};
}

bool LoopMarkerInteraction::isDragging() const {
    return draggingStart_ || draggingEnd_ || draggingRegion_;
}

bool LoopMarkerInteraction::isOnMarker(int x, bool& isStart) const {
    if (!host_.timeToPixel)
        return false;

    int startX = host_.timeToPixel(startTime_);
    int endX = host_.timeToPixel(endTime_);

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
    if (!host_.timeToPixel)
        return false;

    // Check Y is near the top border line
    if (std::abs(y - host_.topBorderY) > host_.topBorderThreshold)
        return false;

    int startX = host_.timeToPixel(startTime_);
    int endX = host_.timeToPixel(endTime_);

    return x > (startX + REGION_HORIZONTAL_MARGIN) && x < (endX - REGION_HORIZONTAL_MARGIN);
}

double LoopMarkerInteraction::applySnap(double time) const {
    if (host_.snapToGrid)
        return host_.snapToGrid(time);
    return time;
}

}  // namespace magda
