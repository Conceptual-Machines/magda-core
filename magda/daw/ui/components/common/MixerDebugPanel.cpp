#include "MixerDebugPanel.hpp"

#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "MixerMetrics.hpp"

namespace magda {

MixerDebugPanel::MixerDebugPanel() {
    auto& metrics = MixerMetrics::getInstance();

    // Create content component for viewport
    contentComponent_ = std::make_unique<juce::Component>();

    // Fader proportions (float)
    addFloatSlider("Thumb Height", &metrics.thumbHeight, 8.0f, 24.0f, 0.5f);
    addFloatSlider("Thumb W Mult", &metrics.thumbWidthMultiplier, 2.0f, 5.0f, 0.1f);
    addFloatSlider("Track W Mult", &metrics.trackWidthMultiplier, 0.2f, 1.0f, 0.02f);
    addFloatSlider("Tick W Mult", &metrics.tickWidthMultiplier, 0.3f, 1.0f, 0.02f);

    // Label dimensions (float)
    addFloatSlider("Label Width", &metrics.labelTextWidth, 10.0f, 30.0f, 0.5f);

    // Channel dimensions (int)
    addIntSlider("Channel Width", &metrics.channelWidth, 80, 200);
    addIntSlider("Fader Width", &metrics.faderWidth, 24, 60);

    // Spacing (int)
    addIntSlider("Tick→Fader Gap", &metrics.tickToFaderGap, -5, 10);
    addIntSlider("Tick→Label Gap", &metrics.tickToLabelGap, -5, 10);

    // Calculate content height
    contentHeight_ = static_cast<int>(rows.size()) * 50 + 10;
    contentComponent_->setSize(220, contentHeight_);

    // Create viewport
    viewport_ = std::make_unique<juce::Viewport>();
    viewport_->setViewedComponent(contentComponent_.get(), false);
    viewport_->setScrollBarsShown(true, false);  // Vertical scroll only
    viewport_->setScrollBarThickness(8);
    addAndMakeVisible(*viewport_);

    // Set initial size (show all content + title bar)
    setSize(240, contentHeight_ + 38);

    // Ensure we receive mouse events
    setInterceptsMouseClicks(true, true);
}

void MixerDebugPanel::paint(juce::Graphics& g) {
    // Semi-transparent dark background
    g.setColour(juce::Colour(0xE0101015));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 8.0f);

    // Border
    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_PURPLE));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1), 8.0f, 2.0f);

    // Resize handle indicator at top
    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY).withAlpha(0.5f));
    int handleWidth = 40;
    int handleX = (getWidth() - handleWidth) / 2;
    g.fillRoundedRectangle(static_cast<float>(handleX), 3.0f, static_cast<float>(handleWidth), 3.0f,
                           1.5f);

    // Title
    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    g.setFont(13.0f);
    g.drawText("Mixer Debug (F12)", 10, 12, getWidth() - 20, 20, juce::Justification::centred);
}

void MixerDebugPanel::resized() {
    const int titleBarHeight = 38;
    const int margin = 10;

    // Viewport takes space below title bar
    viewport_->setBounds(margin, titleBarHeight, getWidth() - margin * 2,
                         getHeight() - titleBarHeight - margin);

    // Layout sliders in content component
    int y = 0;
    const int rowHeight = 50;
    const int labelHeight = 16;
    const int sliderHeight = 24;
    const int sliderMargin = 0;

    for (auto& row : rows) {
        row.label->setBounds(sliderMargin, y, contentComponent_->getWidth() - sliderMargin * 2,
                             labelHeight);
        row.slider->setBounds(sliderMargin, y + labelHeight + 2,
                              contentComponent_->getWidth() - sliderMargin * 2, sliderHeight);
        y += rowHeight;
    }

    // Update content component size
    contentComponent_->setSize(
        viewport_->getWidth() - (viewport_->isVerticalScrollBarShown() ? 8 : 0), contentHeight_);
}

bool MixerDebugPanel::isInResizeZone(const juce::Point<int>& pos) const {
    return pos.y < resizeZoneHeight_;
}

bool MixerDebugPanel::isInDragZone(const juce::Point<int>& pos) const {
    return pos.y >= resizeZoneHeight_ && pos.y < titleBarHeight_;
}

void MixerDebugPanel::mouseMove(const juce::MouseEvent& event) {
    if (isInResizeZone(event.getPosition())) {
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    } else if (isInDragZone(event.getPosition())) {
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    } else {
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }
}

void MixerDebugPanel::mouseDown(const juce::MouseEvent& event) {
    if (isInResizeZone(event.getPosition())) {
        isResizing_ = true;
        isDragging_ = false;
        dragStartY_ = event.getScreenY();
        dragStartHeight_ = getHeight();
    } else if (isInDragZone(event.getPosition())) {
        isDragging_ = true;
        isResizing_ = false;
        dragStartX_ = event.getScreenX() - getX();
        dragStartY_ = event.getScreenY() - getY();
    }
}

void MixerDebugPanel::mouseDrag(const juce::MouseEvent& event) {
    if (isResizing_) {
        // Dragging up (negative delta) should increase height
        // Dragging down (positive delta) should decrease height
        int deltaY = event.getScreenY() - dragStartY_;
        int newHeight = juce::jlimit(minPanelHeight_, maxPanelHeight_, dragStartHeight_ - deltaY);

        // Calculate new Y position to keep bottom edge fixed
        int deltaHeight = getHeight() - newHeight;
        int newY = getY() + deltaHeight;

        setBounds(getX(), newY, getWidth(), newHeight);
    } else if (isDragging_) {
        // Move the panel
        int newX = event.getScreenX() - dragStartX_;
        int newY = event.getScreenY() - dragStartY_;

        // Keep within parent bounds
        if (auto* parent = getParentComponent()) {
            newX = juce::jlimit(0, parent->getWidth() - getWidth(), newX);
            newY = juce::jlimit(0, parent->getHeight() - getHeight(), newY);
        }

        setTopLeftPosition(newX, newY);
    }
}

void MixerDebugPanel::mouseUp(const juce::MouseEvent& /*event*/) {
    isResizing_ = false;
    isDragging_ = false;
}

void MixerDebugPanel::addIntSlider(const juce::String& name, int* valuePtr, int min, int max) {
    SliderRow row;
    row.intValuePtr = valuePtr;
    row.floatValuePtr = nullptr;
    row.isFloat = false;

    row.label = std::make_unique<juce::Label>();
    row.label->setText(name + ": " + juce::String(*valuePtr), juce::dontSendNotification);
    row.label->setColour(juce::Label::textColourId,
                         DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    row.label->setFont(FontManager::getInstance().getUIFont(11.0f));
    contentComponent_->addAndMakeVisible(*row.label);

    row.slider =
        std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
    row.slider->setRange(min, max, 1);
    row.slider->setValue(*valuePtr, juce::dontSendNotification);
    row.slider->setColour(juce::Slider::backgroundColourId,
                          DarkTheme::getColour(DarkTheme::SURFACE));
    row.slider->setColour(juce::Slider::trackColourId,
                          DarkTheme::getColour(DarkTheme::ACCENT_PURPLE));
    row.slider->setColour(juce::Slider::thumbColourId,
                          DarkTheme::getColour(DarkTheme::ACCENT_PURPLE).brighter());

    auto* labelPtr = row.label.get();
    auto* slider = row.slider.get();

    row.slider->onValueChange = [this, valuePtr, labelPtr, slider, name]() {
        *valuePtr = static_cast<int>(slider->getValue());
        labelPtr->setText(name + ": " + juce::String(*valuePtr), juce::dontSendNotification);
        if (onMetricsChanged) {
            onMetricsChanged();
        }
    };

    contentComponent_->addAndMakeVisible(*row.slider);
    rows.push_back(std::move(row));
}

void MixerDebugPanel::addFloatSlider(const juce::String& name, float* valuePtr, float min,
                                     float max, float interval) {
    SliderRow row;
    row.intValuePtr = nullptr;
    row.floatValuePtr = valuePtr;
    row.isFloat = true;

    row.label = std::make_unique<juce::Label>();
    row.label->setText(name + ": " + juce::String(*valuePtr, 2), juce::dontSendNotification);
    row.label->setColour(juce::Label::textColourId,
                         DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    row.label->setFont(FontManager::getInstance().getUIFont(11.0f));
    contentComponent_->addAndMakeVisible(*row.label);

    row.slider =
        std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
    row.slider->setRange(min, max, interval);
    row.slider->setValue(*valuePtr, juce::dontSendNotification);
    row.slider->setColour(juce::Slider::backgroundColourId,
                          DarkTheme::getColour(DarkTheme::SURFACE));
    row.slider->setColour(juce::Slider::trackColourId,
                          DarkTheme::getColour(DarkTheme::ACCENT_PURPLE));
    row.slider->setColour(juce::Slider::thumbColourId,
                          DarkTheme::getColour(DarkTheme::ACCENT_PURPLE).brighter());

    auto* labelPtr = row.label.get();
    auto* slider = row.slider.get();

    row.slider->onValueChange = [this, valuePtr, labelPtr, slider, name]() {
        *valuePtr = static_cast<float>(slider->getValue());
        labelPtr->setText(name + ": " + juce::String(*valuePtr, 2), juce::dontSendNotification);
        if (onMetricsChanged) {
            onMetricsChanged();
        }
    };

    contentComponent_->addAndMakeVisible(*row.slider);
    rows.push_back(std::move(row));
}

}  // namespace magda
