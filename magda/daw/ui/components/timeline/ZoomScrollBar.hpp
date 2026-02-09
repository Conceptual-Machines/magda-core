#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace magda {

/**
 * Combined scroll/zoom bar for timeline and track navigation.
 * - Drag the thumb to scroll
 * - Drag the start/end edges to zoom (shrink = zoom in, expand = zoom out)
 * - Supports both horizontal (timeline) and vertical (tracks) orientations
 */
class ZoomScrollBar : public juce::Component {
  public:
    enum class Orientation { Horizontal, Vertical };

    explicit ZoomScrollBar(Orientation orientation = Orientation::Horizontal);
    ~ZoomScrollBar() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Mouse handling
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;

    // Set the visible range (0.0 to 1.0 representing portion of content)
    void setVisibleRange(double start, double end);

    // Get the current visible range
    double getVisibleStart() const {
        return visibleStart;
    }
    double getVisibleEnd() const {
        return visibleEnd;
    }

    // Get orientation
    Orientation getOrientation() const {
        return orientation;
    }

    // Optional label displayed on the scroll bar (e.g., grid division "1/4")
    void setLabel(const juce::String& text);
    juce::String getLabel() const {
        return label;
    }

    // Callbacks
    std::function<void(double start, double end)> onRangeChanged;

  private:
    Orientation orientation;

    // Optional label text
    juce::String label;

    // Visible range as fraction of total content (0.0 to 1.0)
    double visibleStart = 0.0;
    double visibleEnd = 1.0;

    // Drag state
    enum class DragMode { None, Scroll, ResizeStart, ResizeEnd };
    DragMode dragMode = DragMode::None;
    int dragStartPos = 0;  // X for horizontal, Y for vertical
    double dragStartVisibleStart = 0.0;
    double dragStartVisibleEnd = 0.0;

    // Layout
    static constexpr int EDGE_HANDLE_SIZE = 8;
    static constexpr int MIN_THUMB_SIZE = 20;

    // Helper methods
    juce::Rectangle<int> getThumbBounds() const;
    juce::Rectangle<int> getTrackBounds() const;
    DragMode getDragModeForPosition(int pos) const;
    void updateCursor(int pos);

    // Orientation-aware coordinate helpers
    int getPrimaryCoord(const juce::MouseEvent& event) const;
    int getPrimarySize(const juce::Rectangle<int>& rect) const;
    int getPrimaryPos(const juce::Rectangle<int>& rect) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ZoomScrollBar)
};

}  // namespace magda
