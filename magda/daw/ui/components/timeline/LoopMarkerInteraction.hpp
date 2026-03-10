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
        std::function<double(int pixel)> pixelToTime;
        std::function<int(double time)> timeToPixel;
        std::function<double(double time)> snapToGrid;  // nullable — no snap if null
        std::function<void(double start, double end)> onLoopChanged;
        std::function<void()> onRepaint;
        double maxTime = 0.0;
        int topBorderY = 0;          // Y position of the flag connecting line
        int topBorderThreshold = 6;  // Vertical hit-test threshold around topBorderY
    };

    void setHost(Host host);
    void setLoopRegion(double start, double end, bool enabled);

    // Delegate mouse events from host component — returns true if handled
    bool mouseDown(int x, int y);
    bool mouseDrag(int x, int y);
    bool mouseUp(int x, int y);
    juce::MouseCursor getCursor(int x, int y) const;

    bool isDragging() const;

    double getStartTime() const {
        return startTime_;
    }
    double getEndTime() const {
        return endTime_;
    }
    bool isEnabled() const {
        return enabled_;
    }

  private:
    Host host_;
    double startTime_ = -1.0;
    double endTime_ = -1.0;
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

    double applySnap(double time) const;
};

}  // namespace magda
