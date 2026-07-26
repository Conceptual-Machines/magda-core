#include "MainViewScrollContainer.hpp"

#include <cmath>

namespace magda {

MainViewScrollContainer::MainViewScrollContainer(ZoomScrollBar::InteractionMode horizontalMode,
                                                 ZoomScrollBar::InteractionMode verticalMode) {
    setInterceptsMouseClicks(false, true);

    horizontalScrollBar_ =
        std::make_unique<ZoomScrollBar>(ZoomScrollBar::Orientation::Horizontal, horizontalMode);
    verticalScrollBar_ =
        std::make_unique<ZoomScrollBar>(ZoomScrollBar::Orientation::Vertical, verticalMode);

    addAndMakeVisible(*horizontalScrollBar_);
    addAndMakeVisible(*verticalScrollBar_);
    horizontalScrollBar_->setVisible(false);
    verticalScrollBar_->setVisible(false);

    configureBarCallbacks(Axis::Horizontal);
    configureBarCallbacks(Axis::Vertical);
}

MainViewScrollContainer::~MainViewScrollContainer() {
    unbindViewport();
}

void MainViewScrollContainer::setAutoHideEnabled(bool enabled) {
    horizontalScrollBar_->setAutoHideEnabled(enabled);
    verticalScrollBar_->setAutoHideEnabled(enabled);
}

void MainViewScrollContainer::setAxisLayout(Axis axis, juce::Rectangle<int> bounds, bool needed) {
    auto& bar = barFor(axis);
    bar.setBounds(needed ? bounds : juce::Rectangle<int>());
    bar.setVisible(needed);
}

void MainViewScrollContainer::setVisibleRange(Axis axis, double start, double end) {
    barFor(axis).setVisibleRange(start, end);
}

void MainViewScrollContainer::reveal(Axis axis) {
    barFor(axis).reveal();
}

void MainViewScrollContainer::setRangeChangedCallback(Axis axis, RangeChangedCallback callback) {
    barFor(axis).onRangeChanged = std::move(callback);
}

void MainViewScrollContainer::setWheelCallback(Axis axis, WheelCallback callback) {
    barFor(axis).onWheelMoved = std::move(callback);
}

void MainViewScrollContainer::bindViewport(juce::Viewport& viewport, bool horizontal, bool vertical,
                                           ViewportScrolledCallback callback) {
    unbindViewport();
    boundViewport_ = &viewport;
    horizontalViewportBinding_ = horizontal;
    verticalViewportBinding_ = vertical;
    viewportScrolledCallback_ = std::move(callback);

    if (horizontalViewportBinding_)
        boundViewport_->getHorizontalScrollBar().addListener(this);
    if (verticalViewportBinding_)
        boundViewport_->getVerticalScrollBar().addListener(this);

    configureBarCallbacks(Axis::Horizontal);
    configureBarCallbacks(Axis::Vertical);
    syncFromViewport();
}

void MainViewScrollContainer::unbindViewport() {
    if (boundViewport_ != nullptr) {
        if (horizontalViewportBinding_)
            boundViewport_->getHorizontalScrollBar().removeListener(this);
        if (verticalViewportBinding_)
            boundViewport_->getVerticalScrollBar().removeListener(this);
    }

    boundViewport_ = nullptr;
    horizontalViewportBinding_ = false;
    verticalViewportBinding_ = false;
    viewportScrolledCallback_ = {};
}

void MainViewScrollContainer::syncFromViewport() {
    if (horizontalViewportBinding_)
        syncAxisFromViewport(Axis::Horizontal);
    if (verticalViewportBinding_)
        syncAxisFromViewport(Axis::Vertical);
}

ZoomScrollBar& MainViewScrollContainer::getScrollBar(Axis axis) {
    return barFor(axis);
}

const ZoomScrollBar& MainViewScrollContainer::getScrollBar(Axis axis) const {
    return barFor(axis);
}

ZoomScrollBar& MainViewScrollContainer::barFor(Axis axis) {
    return axis == Axis::Horizontal ? *horizontalScrollBar_ : *verticalScrollBar_;
}

const ZoomScrollBar& MainViewScrollContainer::barFor(Axis axis) const {
    return axis == Axis::Horizontal ? *horizontalScrollBar_ : *verticalScrollBar_;
}

void MainViewScrollContainer::configureBarCallbacks(Axis axis) {
    const bool isBound =
        axis == Axis::Horizontal ? horizontalViewportBinding_ : verticalViewportBinding_;
    if (!isBound || boundViewport_ == nullptr)
        return;

    barFor(axis).onRangeChanged = [this, axis](double start, double /*end*/) {
        applyBoundRange(axis, start);
    };
    barFor(axis).onWheelMoved = [this](const juce::MouseEvent& event,
                                       const juce::MouseWheelDetails& wheel) {
        if (boundViewport_ != nullptr) {
            boundViewport_->mouseWheelMove(event.getEventRelativeTo(boundViewport_), wheel);
        }
    };
}

void MainViewScrollContainer::applyBoundRange(Axis axis, double normalizedStart) {
    if (boundViewport_ == nullptr)
        return;

    const auto& source = axis == Axis::Horizontal ? boundViewport_->getHorizontalScrollBar()
                                                  : boundViewport_->getVerticalScrollBar();
    const double rangeStart = source.getMinimumRangeLimit();
    const double rangeLength = source.getMaximumRangeLimit() - rangeStart;
    const int target = static_cast<int>(std::round(rangeStart + normalizedStart * rangeLength));

    auto position = boundViewport_->getViewPosition();
    if (axis == Axis::Horizontal)
        position.setX(target);
    else
        position.setY(target);
    boundViewport_->setViewPosition(position);

    syncAxisFromViewport(axis);
    if (viewportScrolledCallback_)
        viewportScrolledCallback_(axis, axis == Axis::Horizontal
                                            ? boundViewport_->getViewPositionX()
                                            : boundViewport_->getViewPositionY());
}

void MainViewScrollContainer::syncAxisFromViewport(Axis axis) {
    if (boundViewport_ == nullptr)
        return;

    const auto& source = axis == Axis::Horizontal ? boundViewport_->getHorizontalScrollBar()
                                                  : boundViewport_->getVerticalScrollBar();
    const double rangeStart = source.getMinimumRangeLimit();
    const double rangeLength = source.getMaximumRangeLimit() - rangeStart;
    if (rangeLength <= 0.0) {
        barFor(axis).setVisibleRange(0.0, 1.0);
        return;
    }

    const auto visibleRange = source.getCurrentRange();
    barFor(axis).setVisibleRange((visibleRange.getStart() - rangeStart) / rangeLength,
                                 (visibleRange.getEnd() - rangeStart) / rangeLength);
}

void MainViewScrollContainer::scrollBarMoved(juce::ScrollBar* scrollBar, double newRangeStart) {
    if (boundViewport_ == nullptr)
        return;

    Axis axis;
    if (horizontalViewportBinding_ && scrollBar == &boundViewport_->getHorizontalScrollBar()) {
        axis = Axis::Horizontal;
    } else if (verticalViewportBinding_ && scrollBar == &boundViewport_->getVerticalScrollBar()) {
        axis = Axis::Vertical;
    } else {
        return;
    }

    syncAxisFromViewport(axis);
    reveal(axis);
    if (viewportScrolledCallback_)
        viewportScrolledCallback_(axis, newRangeStart);
}

}  // namespace magda
