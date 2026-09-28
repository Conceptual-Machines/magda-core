#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace magda {

/**
 * Debug panel for adjusting MixerMetrics values in real-time.
 * Press F12 to toggle visibility.
 * Drag the top edge to resize.
 */
class MixerDebugPanel : public juce::Component {
  public:
    MixerDebugPanel();
    ~MixerDebugPanel() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

    // Get the full content height (for parent to know ideal size)
    int getContentHeight() const {
        return contentHeight_;
    }

    // Callback when any value changes
    std::function<void()> onMetricsChanged;

  private:
    struct SliderRow {
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::Slider> slider;
        int* intValuePtr = nullptr;
        float* floatValuePtr = nullptr;
        bool isFloat;
    };

    std::vector<SliderRow> rows;

    // Viewport for scrollable content
    std::unique_ptr<juce::Viewport> viewport_;
    std::unique_ptr<juce::Component> contentComponent_;

    // Resize and drag state
    static constexpr int resizeZoneHeight_ = 10;
    static constexpr int titleBarHeight_ = 38;
    static constexpr int minPanelHeight_ = 150;
    static constexpr int maxPanelHeight_ = 600;
    bool isResizing_ = false;
    bool isDragging_ = false;
    int dragStartX_ = 0;
    int dragStartY_ = 0;
    int dragStartHeight_ = 0;
    int contentHeight_ = 0;

    static bool isInResizeZone(const juce::Point<int>& pos);
    static bool isInDragZone(const juce::Point<int>& pos);

    void addIntSlider(const juce::String& name, int* valuePtr, int min, int max);
    void addFloatSlider(const juce::String& name, float* valuePtr, float min, float max,
                        float interval = 0.01f);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerDebugPanel)
};

}  // namespace magda
