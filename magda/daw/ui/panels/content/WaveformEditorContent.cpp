#include "WaveformEditorContent.hpp"

#include "../../state/TimelineController.hpp"
#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"

namespace magda::daw::ui {

// ============================================================================
// ScrollNotifyingViewport - Custom viewport that notifies on scroll
// ============================================================================

class WaveformEditorContent::ScrollNotifyingViewport : public juce::Viewport {
  public:
    std::function<void(int, int)> onScrolled;
    juce::Component* timeRulerToRepaint = nullptr;

    void visibleAreaChanged(const juce::Rectangle<int>& newVisibleArea) override {
        juce::Viewport::visibleAreaChanged(newVisibleArea);
        if (onScrolled) {
            onScrolled(getViewPositionX(), getViewPositionY());
        }
        if (timeRulerToRepaint) {
            timeRulerToRepaint->repaint();
        }
    }

    void scrollBarMoved(juce::ScrollBar* scrollBar, double newRangeStart) override {
        juce::Viewport::scrollBarMoved(scrollBar, newRangeStart);
        if (timeRulerToRepaint) {
            timeRulerToRepaint->repaint();
        }
    }
};

// ============================================================================
// ButtonLookAndFeel - Custom look and feel for mode toggle button
// ============================================================================

class WaveformEditorContent::ButtonLookAndFeel : public juce::LookAndFeel_V4 {
  public:
    ButtonLookAndFeel() {
        setColour(juce::TextButton::buttonColourId, DarkTheme::getColour(DarkTheme::SURFACE));
        setColour(juce::TextButton::buttonOnColourId, DarkTheme::getAccentColour().withAlpha(0.3f));
        setColour(juce::TextButton::textColourOffId, DarkTheme::getTextColour());
        setColour(juce::TextButton::textColourOnId, DarkTheme::getAccentColour());
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                              const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override {
        auto bounds = button.getLocalBounds().toFloat();
        auto baseColour = backgroundColour;

        if (shouldDrawButtonAsDown || button.getToggleState()) {
            baseColour = button.findColour(juce::TextButton::buttonOnColourId);
        } else if (shouldDrawButtonAsHighlighted) {
            baseColour = baseColour.brighter(0.1f);
        }

        g.setColour(baseColour);
        g.fillRoundedRectangle(bounds, 3.0f);

        if (button.hasKeyboardFocus(false)) {
            g.setColour(DarkTheme::getAccentColour().withAlpha(0.5f));
            g.drawRoundedRectangle(bounds.reduced(1.0f), 3.0f, 1.0f);
        }
    }
};

// ============================================================================
// Constructor / Destructor
// ============================================================================

WaveformEditorContent::WaveformEditorContent() {
    setName("WaveformEditor");

    // Register as ClipManager listener
    magda::ClipManager::getInstance().addListener(this);

    // Create time ruler
    timeRuler_ = std::make_unique<magda::TimeRuler>();
    timeRuler_->setDisplayMode(magda::TimeRuler::DisplayMode::BarsBeats);
    timeRuler_->setRelativeMode(relativeTimeMode_);
    timeRuler_->setLeftPadding(GRID_LEFT_PADDING);
    addAndMakeVisible(timeRuler_.get());

    // Create look and feel for buttons
    buttonLookAndFeel_ = std::make_unique<ButtonLookAndFeel>();

    // Create time mode toggle button
    timeModeButton_ = std::make_unique<juce::TextButton>("REL");
    timeModeButton_->setTooltip("Toggle between Relative (clip) and Absolute (timeline) mode");
    timeModeButton_->setClickingTogglesState(true);
    timeModeButton_->setToggleState(relativeTimeMode_, juce::dontSendNotification);
    timeModeButton_->setLookAndFeel(buttonLookAndFeel_.get());
    timeModeButton_->onClick = [this]() { setRelativeTimeMode(timeModeButton_->getToggleState()); };
    addAndMakeVisible(timeModeButton_.get());

    // Create waveform grid component
    gridComponent_ = std::make_unique<WaveformGridComponent>();
    gridComponent_->setRelativeMode(relativeTimeMode_);
    gridComponent_->setHorizontalZoom(horizontalZoom_);

    // Create viewport and add grid
    viewport_ = std::make_unique<ScrollNotifyingViewport>();
    viewport_->setViewedComponent(gridComponent_.get(), false);
    viewport_->setScrollBarsShown(true, true);
    viewport_->timeRulerToRepaint = timeRuler_.get();
    addAndMakeVisible(viewport_.get());

    // Setup scroll callback
    viewport_->onScrolled = [this](int x, int y) {
        timeRuler_->setScrollOffset(x);
        gridComponent_->setScrollOffset(x, y);
    };

    // Callback when waveform is edited
    gridComponent_->onWaveformChanged = [this]() {
        // Could add logic here if needed
    };

    // Check if there's already a selected audio clip
    magda::ClipId selectedClip = magda::ClipManager::getInstance().getSelectedClip();
    if (selectedClip != magda::INVALID_CLIP_ID) {
        const auto* clip = magda::ClipManager::getInstance().getClip(selectedClip);
        if (clip && clip->type == magda::ClipType::Audio) {
            setClip(selectedClip);
        }
    }
}

WaveformEditorContent::~WaveformEditorContent() {
    magda::ClipManager::getInstance().removeListener(this);

    // Clear look and feel before destruction
    if (timeModeButton_) {
        timeModeButton_->setLookAndFeel(nullptr);
    }
}

// ============================================================================
// Layout
// ============================================================================

void WaveformEditorContent::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getPanelBackgroundColour());
}

void WaveformEditorContent::resized() {
    auto bounds = getLocalBounds();

    // Toolbar at top
    auto toolbarArea = bounds.removeFromTop(TOOLBAR_HEIGHT);
    timeModeButton_->setBounds(toolbarArea.removeFromLeft(60).reduced(2));

    // Time ruler below toolbar
    auto rulerArea = bounds.removeFromTop(TIME_RULER_HEIGHT);
    timeRuler_->setBounds(rulerArea);

    // Viewport fills remaining space
    viewport_->setBounds(bounds);

    // Update grid size
    updateGridSize();
}

// ============================================================================
// Panel Lifecycle
// ============================================================================

void WaveformEditorContent::onActivated() {
    // Check for selected audio clip
    magda::ClipId selectedClip = magda::ClipManager::getInstance().getSelectedClip();
    if (selectedClip != magda::INVALID_CLIP_ID) {
        const auto* clip = magda::ClipManager::getInstance().getClip(selectedClip);
        if (clip && clip->type == magda::ClipType::Audio) {
            setClip(selectedClip);
        }
    }
}

void WaveformEditorContent::onDeactivated() {
    // Nothing to do
}

// ============================================================================
// Mouse Interaction
// ============================================================================

void WaveformEditorContent::mouseWheelMove(const juce::MouseEvent& event,
                                           const juce::MouseWheelDetails& wheel) {
    // Cmd/Ctrl + scroll = zoom
    if (event.mods.isCommandDown()) {
        double zoomFactor = 1.0 + (wheel.deltaY * 0.5);
        int anchorX = event.x - viewport_->getX();
        performAnchorPointZoom(zoomFactor, anchorX);
    } else {
        // Normal scroll - let viewport handle it
        Component::mouseWheelMove(event, wheel);
    }
}

// ============================================================================
// ClipManagerListener
// ============================================================================

void WaveformEditorContent::clipsChanged() {
    // Check if our clip was deleted
    if (editingClipId_ != magda::INVALID_CLIP_ID) {
        const auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
        if (!clip) {
            editingClipId_ = magda::INVALID_CLIP_ID;
            gridComponent_->setClip(magda::INVALID_CLIP_ID);
        }
    }
}

void WaveformEditorContent::clipPropertyChanged(magda::ClipId clipId) {
    if (clipId == editingClipId_) {
        // Re-set the clip to refresh clipStartTime and clipLength in grid
        const auto* clip = magda::ClipManager::getInstance().getClip(clipId);
        if (clip) {
            gridComponent_->setClip(clipId);

            // Update time ruler with new clip position
            double bpm = 120.0;
            auto* controller = magda::TimelineController::getCurrent();
            if (controller) {
                bpm = controller->getState().tempo.bpm;
            }

            timeRuler_->setZoom(horizontalZoom_);
            timeRuler_->setTempo(bpm);
            timeRuler_->setTimeOffset(clip->startTime);
            timeRuler_->setClipLength(clip->length);

            // Scroll viewport to show clip at new position
            scrollToClipStart();
        }

        updateGridSize();
        repaint();
    }
}

void WaveformEditorContent::clipSelectionChanged(magda::ClipId clipId) {
    // Auto-switch to the selected clip if it's an audio clip
    if (clipId != magda::INVALID_CLIP_ID) {
        const auto* clip = magda::ClipManager::getInstance().getClip(clipId);
        if (clip && clip->type == magda::ClipType::Audio) {
            setClip(clipId);
        }
    }
}

// ============================================================================
// Public Methods
// ============================================================================

void WaveformEditorContent::setClip(magda::ClipId clipId) {
    if (editingClipId_ != clipId) {
        editingClipId_ = clipId;
        gridComponent_->setClip(clipId);

        // Update time ruler with clip info
        const auto* clip = magda::ClipManager::getInstance().getClip(clipId);
        if (clip) {
            // Get tempo from TimelineController
            double bpm = 120.0;
            auto* controller = magda::TimelineController::getCurrent();
            if (controller) {
                bpm = controller->getState().tempo.bpm;
            }

            timeRuler_->setZoom(horizontalZoom_);
            timeRuler_->setTempo(bpm);
            timeRuler_->setTimeOffset(clip->startTime);
            timeRuler_->setClipLength(clip->length);
        }

        updateGridSize();
        scrollToClipStart();
        repaint();
    }
}

void WaveformEditorContent::setRelativeTimeMode(bool relative) {
    if (relativeTimeMode_ != relative) {
        relativeTimeMode_ = relative;

        // Update button text
        timeModeButton_->setButtonText(relative ? "REL" : "ABS");
        timeModeButton_->setToggleState(relative, juce::dontSendNotification);

        // Update components
        gridComponent_->setRelativeMode(relative);
        timeRuler_->setRelativeMode(relative);

        // Update grid size and scroll
        updateGridSize();
        scrollToClipStart();
        repaint();
    }
}

// ============================================================================
// Private Helpers
// ============================================================================

void WaveformEditorContent::updateGridSize() {
    if (gridComponent_) {
        gridComponent_->updateGridSize();

        // Update time ruler length
        const auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
        if (clip && timeRuler_) {
            double totalTime = relativeTimeMode_ ? clip->length : (clip->startTime + clip->length);
            timeRuler_->setTimelineLength(totalTime + 10.0);  // Add padding
        }
    }
}

void WaveformEditorContent::scrollToClipStart() {
    if (relativeTimeMode_) {
        // In relative mode, scroll to beginning
        viewport_->setViewPosition(0, viewport_->getViewPositionY());
    } else {
        // In absolute mode, scroll to clip start position
        const auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
        if (clip && gridComponent_) {
            int clipStartX = gridComponent_->timeToPixel(clip->startTime);
            viewport_->setViewPosition(clipStartX, viewport_->getViewPositionY());
        }
    }
}

void WaveformEditorContent::performAnchorPointZoom(double zoomFactor, int anchorX) {
    // Calculate anchor point in content space
    int mouseXInContent = anchorX + viewport_->getViewPositionX();
    double anchorTime = 0.0;
    if (gridComponent_) {
        anchorTime = gridComponent_->pixelToTime(mouseXInContent);
    }

    // Apply zoom
    double newZoom = horizontalZoom_ * zoomFactor;
    newZoom = juce::jlimit(MIN_ZOOM, MAX_ZOOM, newZoom);

    if (newZoom != horizontalZoom_) {
        horizontalZoom_ = newZoom;

        // Update grid component
        gridComponent_->setHorizontalZoom(horizontalZoom_);

        // Update time ruler
        const auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
        if (clip && timeRuler_) {
            double bpm = 120.0;
            auto* controller = magda::TimelineController::getCurrent();
            if (controller) {
                bpm = controller->getState().tempo.bpm;
            }
            timeRuler_->setZoom(horizontalZoom_);
            timeRuler_->setTempo(bpm);
        }

        updateGridSize();

        // Adjust scroll to keep anchor point under mouse
        if (gridComponent_) {
            int newAnchorX = gridComponent_->timeToPixel(anchorTime);
            int newScrollX = newAnchorX - anchorX;
            viewport_->setViewPosition(newScrollX, viewport_->getViewPositionY());
        }
    }
}

}  // namespace magda::daw::ui
