#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

#include "../timeline/ZoomScrollBar.hpp"

namespace magda {

/** Viewport variant that makes bubbled macOS wheel/trackpad events consumable by JUCE. */
class WheelForwardingViewport : public juce::Viewport {
  public:
    void mouseWheelMove(const juce::MouseEvent& event,
                        const juce::MouseWheelDetails& wheel) override {
        juce::Viewport::mouseWheelMove(event.getEventRelativeTo(this), wheel);
    }
};

/**
 * Shared scrollbar host for the Arrangement, Session, and Mixer views.
 *
 * The owning view supplies the scrollbar slots because each primary view has different pinned
 * columns/rows. This component owns the common bars, auto-hide behaviour, normalized range
 * binding, and wheel forwarding. It can bind directly to a JUCE viewport for scroll-only views,
 * or accept custom range callbacks for zoom-aware views such as Arrangement.
 */
class MainViewScrollContainer : public juce::Component, private juce::ScrollBar::Listener {
  public:
    enum class Axis { Horizontal, Vertical };
    using RangeChangedCallback = std::function<void(double start, double end)>;
    using ViewportScrolledCallback = std::function<void(Axis axis, double rangeStart)>;
    using WheelCallback =
        std::function<void(const juce::MouseEvent&, const juce::MouseWheelDetails&)>;

    explicit MainViewScrollContainer(
        ZoomScrollBar::InteractionMode horizontalMode = ZoomScrollBar::InteractionMode::ScrollOnly,
        ZoomScrollBar::InteractionMode verticalMode = ZoomScrollBar::InteractionMode::ScrollOnly);
    ~MainViewScrollContainer() override;

    void setAutoHideEnabled(bool enabled);
    void setAxisLayout(Axis axis, juce::Rectangle<int> bounds, bool needed);
    void setVisibleRange(Axis axis, double start, double end);
    void reveal(Axis axis);

    void setRangeChangedCallback(Axis axis, RangeChangedCallback callback);
    void setWheelCallback(Axis axis, WheelCallback callback);

    void bindViewport(juce::Viewport& viewport, bool horizontal, bool vertical,
                      ViewportScrolledCallback callback = {});
    void unbindViewport();
    void syncFromViewport();

    ZoomScrollBar& getScrollBar(Axis axis);
    const ZoomScrollBar& getScrollBar(Axis axis) const;

  private:
    std::unique_ptr<ZoomScrollBar> horizontalScrollBar_;
    std::unique_ptr<ZoomScrollBar> verticalScrollBar_;
    juce::Viewport* boundViewport_ = nullptr;
    bool horizontalViewportBinding_ = false;
    bool verticalViewportBinding_ = false;
    ViewportScrolledCallback viewportScrolledCallback_;

    ZoomScrollBar& barFor(Axis axis);
    const ZoomScrollBar& barFor(Axis axis) const;
    void configureBarCallbacks(Axis axis);
    void applyBoundRange(Axis axis, double normalizedStart);
    void syncAxisFromViewport(Axis axis);
    void scrollBarMoved(juce::ScrollBar* scrollBar, double newRangeStart) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainViewScrollContainer)
};

}  // namespace magda
