#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace magda::daw::ui {

/**
 * @brief A horizontal scrollbar for a viewport whose own bar is hidden, placed by its owner
 * outside the viewport so the content keeps its full height (#3141).
 *
 * Always shown, its thumb full width while everything fits. The viewport and its viewed
 * component must outlive it.
 */
class ViewportScrollBar : public juce::ScrollBar,
                          private juce::ScrollBar::Listener,
                          private juce::ComponentListener {
  public:
    explicit ViewportScrollBar(juce::Viewport& viewport)
        : juce::ScrollBar(false), viewport_(viewport), viewed_(viewport.getViewedComponent()) {
        setAutoHide(false);
        juce::ScrollBar::addListener(this);
        viewport_.addComponentListener(this);
        if (viewed_ != nullptr)
            viewed_->addComponentListener(this);
        sync();
    }

    ~ViewportScrollBar() override {
        viewport_.removeComponentListener(this);
        if (viewed_ != nullptr)
            viewed_->removeComponentListener(this);
    }

    /** @brief Matches the bar to the viewport's content width and scroll position. */
    void sync() {
        const double total = viewed_ != nullptr ? viewed_->getWidth() : 0.0;
        setRangeLimits(0.0, total, juce::dontSendNotification);
        setCurrentRange(viewport_.getViewPositionX(), viewport_.getViewWidth(),
                        juce::dontSendNotification);
    }

    // The viewport's own wheel handling, so scrolling over the bar matches scrolling over content.
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override {
        viewport_.mouseWheelMove(e.getEventRelativeTo(&viewport_), wheel);
    }

  private:
    void scrollBarMoved(juce::ScrollBar*, double newRangeStart) override {
        viewport_.setViewPosition(juce::roundToInt(newRangeStart), viewport_.getViewPositionY());
    }

    // The viewed component moves as the viewport scrolls, and resizes as content changes.
    void componentMovedOrResized(juce::Component&, bool, bool) override {
        sync();
    }

    juce::Viewport& viewport_;
    juce::Component* viewed_;
};

}  // namespace magda::daw::ui
