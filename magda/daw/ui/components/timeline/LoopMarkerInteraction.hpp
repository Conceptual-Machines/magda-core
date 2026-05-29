#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace magda {

/**
 * Reusable helper that encapsulates loop-marker drag logic.
 * Host components (TimelineComponent, TimeRuler) delegate mouse events
 * to this class and provide coordinate conversion via the Host struct.
 */
class LoopMarkerInteraction {
  public:
    struct Host {
        std::function<double(int pixel)> pixelToPosition;
        std::function<int(double position)> positionToPixel;
        std::function<double(double position)> snapPosition;  // nullable — no snap if null
        std::function<void(double start, double end)> onLoopChanged;
        std::function<void()> onRepaint;
        double maxPosition = 0.0;
        int topBorderY = 0;          // Y position of the flag connecting line (top of strip)
        int topBorderThreshold = 6;  // How far BELOW topBorderY still counts as the strip body
                                     // (a small fixed tolerance above the line is added internally)
    };

    void setHost(Host host);
    void setLoopRange(double start, double end, bool enabled);

    // Delegate mouse events from host component — returns true if handled
    bool mouseDown(int x, int y);
    bool mouseDrag(int x, int y);
    bool mouseUp(int x, int y);
    juce::MouseCursor getCursor(int x, int y) const;

    bool isDragging() const;

    double getStartPosition() const {
        return startPosition_;
    }
    double getEndPosition() const {
        return endPosition_;
    }
    bool isEnabled() const {
        return enabled_;
    }

  private:
    Host host_;
    double startPosition_ = -1.0;
    double endPosition_ = -1.0;
    bool enabled_ = false;

    bool draggingStart_ = false;
    bool draggingEnd_ = false;
    bool draggingRegion_ = false;
    double dragOffset_ = 0.0;

    static constexpr int HIT_THRESHOLD = 8;
    static constexpr int FLAG_HEIGHT = 12;
    static constexpr int REGION_HORIZONTAL_MARGIN = 10;

    bool isOnMarker(int x, bool& isStart) const;
    bool isOnMarker(int x, int y, bool& isStart) const;
    bool isOnTopBorder(int x, int y) const;

    double applySnap(double position) const;
};

}  // namespace magda
