#include "WaveformEditorContent.hpp"

#include <cmath>

#include "../../state/TimelineController.hpp"
#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "audio/AudioBridge.hpp"
#include "audio/AudioThumbnailManager.hpp"
#include "core/ClipDisplayInfo.hpp"
#include "core/TrackManager.hpp"
#include "engine/AudioEngine.hpp"

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

    juce::Font getTextButtonFont(juce::TextButton&, int /*buttonHeight*/) override {
        return magda::FontManager::getInstance().getButtonFont(11.0f);
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

        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.drawRoundedRectangle(bounds, 3.0f, 1.0f);
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& button, bool /*isMouseOver*/,
                        bool /*isButtonDown*/) override {
        auto font = magda::FontManager::getInstance().getButtonFont(11.0f);
        g.setFont(font);
        g.setColour(button.findColour(button.getToggleState() ? juce::TextButton::textColourOnId
                                                              : juce::TextButton::textColourOffId));
        g.drawText(button.getButtonText(), button.getLocalBounds(), juce::Justification::centred);
    }
};

// ============================================================================
// PlayheadOverlay - Renders playhead over the waveform viewport
// ============================================================================

class WaveformEditorContent::PlayheadOverlay : public juce::Component {
  public:
    explicit PlayheadOverlay(WaveformEditorContent& owner) : owner_(owner) {
        setInterceptsMouseClicks(false, false);
    }

    void paint(juce::Graphics& g) override {
        if (getWidth() <= 0 || getHeight() <= 0)
            return;
        if (owner_.editingClipId_ == magda::INVALID_CLIP_ID)
            return;

        const auto* clip = magda::ClipManager::getInstance().getClip(owner_.editingClipId_);
        if (!clip)
            return;

        int scrollX = owner_.viewport_->getViewPositionX();

        // Helper to convert a timeline time to pixel X in our overlay coordinate space
        auto timeToOverlayX = [&](double time) -> int {
            double displayTime = owner_.relativeTimeMode_ ? (time - clip->startTime) : time;
            return static_cast<int>(displayTime * owner_.horizontalZoom_) + GRID_LEFT_PADDING -
                   scrollX;
        };

        // Draw edit cursor (triangle at top) - always visible
        double editPos = owner_.cachedEditPosition_;
        int editX = timeToOverlayX(editPos);
        if (editX >= 0 && editX < getWidth()) {
            g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_RED));
            juce::Path triangle;
            triangle.addTriangle(static_cast<float>(editX - 5), 0.0f, static_cast<float>(editX + 5),
                                 0.0f, static_cast<float>(editX), 10.0f);
            g.fillPath(triangle);
        }

        // Draw playback cursor (vertical line) - only during playback
        if (owner_.cachedIsPlaying_) {
            double playPos = owner_.cachedPlaybackPosition_;

            // Wrap playhead inside loop region when looping is enabled
            double srcLength = clip->loopLength;
            if (clip->loopEnabled && srcLength > 0.0) {
                double bpm = 120.0;
                auto* controller = magda::TimelineController::getCurrent();
                if (controller) {
                    bpm = controller->getState().tempo.bpm;
                }
                auto di = magda::ClipDisplayInfo::from(*clip, bpm);

                if (di.loopLengthSeconds > 0.0) {
                    // Position relative to clip start, wrapped within loop cycle
                    double relPos = playPos - clip->startTime;
                    if (relPos >= 0.0) {
                        relPos = std::fmod(relPos, di.loopLengthSeconds);
                        playPos = clip->startTime + relPos;
                    }
                }
            }

            int playX = timeToOverlayX(playPos);
            if (playX >= 0 && playX < getWidth()) {
                g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_RED));
                g.drawLine(static_cast<float>(playX), 0.0f, static_cast<float>(playX),
                           static_cast<float>(getHeight()), 1.5f);
            }
        }
    }

  private:
    WaveformEditorContent& owner_;
    static constexpr int GRID_LEFT_PADDING = 10;
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
    timeModeButton_ = std::make_unique<juce::TextButton>("ABS");
    timeModeButton_->setTooltip("Toggle between Absolute (timeline) and Relative (clip) mode");
    timeModeButton_->setClickingTogglesState(true);
    timeModeButton_->setToggleState(relativeTimeMode_, juce::dontSendNotification);
    timeModeButton_->setLookAndFeel(buttonLookAndFeel_.get());
    timeModeButton_->onClick = [this]() { setRelativeTimeMode(timeModeButton_->getToggleState()); };
    addAndMakeVisible(timeModeButton_.get());

    // Create WARP mode toggle button
    warpModeButton_ = std::make_unique<juce::TextButton>("WARP");
    warpModeButton_->setTooltip("Toggle warp mode (add/move warp markers)");
    warpModeButton_->setClickingTogglesState(true);
    warpModeButton_->setToggleState(false, juce::dontSendNotification);
    warpModeButton_->setLookAndFeel(buttonLookAndFeel_.get());
    warpModeButton_->onClick = [this]() {
        if (editingClipId_ != magda::INVALID_CLIP_ID) {
            bool newState = warpModeButton_->getToggleState();
            magda::ClipManager::getInstance().setClipWarpEnabled(editingClipId_, newState);
        }
    };
    addAndMakeVisible(warpModeButton_.get());

    // Create BPM label for toolbar
    bpmLabel_ =
        std::make_unique<juce::Label>("bpmLabel", juce::String::fromUTF8("\xe2\x80\x94 BPM"));
    bpmLabel_->setFont(magda::FontManager::getInstance().getUIFont(11.0f));
    bpmLabel_->setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    bpmLabel_->setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(bpmLabel_.get());

    // Create grid resolution combo box
    gridResolutionCombo_ = std::make_unique<juce::ComboBox>("gridResolution");
    gridResolutionCombo_->addItem("Off", 1);
    gridResolutionCombo_->addItem("Bar", 2);
    gridResolutionCombo_->addItem("Beat", 3);
    gridResolutionCombo_->addItem("1/8", 4);
    gridResolutionCombo_->addItem("1/16", 5);
    gridResolutionCombo_->addItem("1/32", 6);
    gridResolutionCombo_->setSelectedId(1, juce::dontSendNotification);
    gridResolutionCombo_->setTooltip("Beat grid resolution for snap and display");
    gridResolutionCombo_->setLookAndFeel(buttonLookAndFeel_.get());
    gridResolutionCombo_->onChange = [this]() {
        auto id = gridResolutionCombo_->getSelectedId();
        GridResolution res = GridResolution::Off;
        switch (id) {
            case 2:
                res = GridResolution::Bar;
                break;
            case 3:
                res = GridResolution::Beat;
                break;
            case 4:
                res = GridResolution::Eighth;
                break;
            case 5:
                res = GridResolution::Sixteenth;
                break;
            case 6:
                res = GridResolution::ThirtySecond;
                break;
            default:
                res = GridResolution::Off;
                break;
        }
        gridComponent_->setGridResolution(res);
    };
    addAndMakeVisible(gridResolutionCombo_.get());

    // Loop ghost toggle
    loopGhostButton_ = std::make_unique<juce::TextButton>("GHOST");
    loopGhostButton_->setTooltip("Show loop repetitions beyond the loop boundary");
    loopGhostButton_->setClickingTogglesState(true);
    loopGhostButton_->setToggleState(false, juce::dontSendNotification);
    loopGhostButton_->setLookAndFeel(buttonLookAndFeel_.get());
    loopGhostButton_->onClick = [this]() {
        if (gridComponent_)
            gridComponent_->setShowLoopGhost(loopGhostButton_->getToggleState());
    };
    addAndMakeVisible(loopGhostButton_.get());

    // Create waveform grid component
    gridComponent_ = std::make_unique<WaveformGridComponent>();
    gridComponent_->setRelativeMode(relativeTimeMode_);
    gridComponent_->setHorizontalZoom(horizontalZoom_);
    gridComponent_->setTimeRuler(timeRuler_.get());

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
        if (playheadOverlay_)
            playheadOverlay_->repaint();
    };

    // Create playhead overlay on top of viewport
    playheadOverlay_ = std::make_unique<PlayheadOverlay>(*this);
    addAndMakeVisible(playheadOverlay_.get());

    // Register as TimelineStateListener
    auto* controller = magda::TimelineController::getCurrent();
    if (controller) {
        controller->addListener(this);
        const auto& state = controller->getState();
        cachedEditPosition_ = state.playhead.editPosition;
        cachedPlaybackPosition_ = state.playhead.playbackPosition;
        cachedIsPlaying_ = state.playhead.isPlaying;
    }

    // Callback when waveform is edited
    gridComponent_->onWaveformChanged = [this]() {
        // Could add logic here if needed
    };

    // Warp marker callbacks
    gridComponent_->onWarpMarkerAdd = [this](double sourceTime, double warpTime) {
        auto* bridge = getBridge();
        if (bridge) {
            bridge->addWarpMarker(editingClipId_, sourceTime, warpTime);
            refreshWarpMarkers();
        }
    };

    gridComponent_->onWarpMarkerMove = [this](int index, double newWarpTime) {
        auto* bridge = getBridge();
        if (bridge) {
            bridge->moveWarpMarker(editingClipId_, index, newWarpTime);
            refreshWarpMarkers();
        }
    };

    gridComponent_->onWarpMarkerRemove = [this](int index) {
        auto* bridge = getBridge();
        if (bridge) {
            bridge->removeWarpMarker(editingClipId_, index);
            refreshWarpMarkers();
        }
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
    stopTimer();

    auto* controller = magda::TimelineController::getCurrent();
    if (controller) {
        controller->removeListener(this);
    }

    magda::ClipManager::getInstance().removeListener(this);

    // Clear look and feel before destruction
    if (timeModeButton_) {
        timeModeButton_->setLookAndFeel(nullptr);
    }
    if (warpModeButton_) {
        warpModeButton_->setLookAndFeel(nullptr);
    }
    if (gridResolutionCombo_) {
        gridResolutionCombo_->setLookAndFeel(nullptr);
    }
    if (loopGhostButton_) {
        loopGhostButton_->setLookAndFeel(nullptr);
    }
}

// ============================================================================
// Layout
// ============================================================================

void WaveformEditorContent::paint(juce::Graphics& g) {
    if (getWidth() <= 0 || getHeight() <= 0)
        return;
    g.fillAll(DarkTheme::getPanelBackgroundColour());
}

void WaveformEditorContent::resized() {
    auto bounds = getLocalBounds();

    // Guard against too-small bounds (panel being resized very small)
    int minHeight = TOOLBAR_HEIGHT + TIME_RULER_HEIGHT + 1;
    if (bounds.getHeight() < minHeight || bounds.getWidth() <= 0) {
        // Hide everything when too small to avoid zero-sized paint
        timeModeButton_->setBounds(0, 0, 0, 0);
        warpModeButton_->setBounds(0, 0, 0, 0);
        gridResolutionCombo_->setBounds(0, 0, 0, 0);
        loopGhostButton_->setBounds(0, 0, 0, 0);
        bpmLabel_->setBounds(0, 0, 0, 0);
        timeRuler_->setBounds(0, 0, 0, 0);
        viewport_->setBounds(0, 0, 0, 0);
        if (playheadOverlay_)
            playheadOverlay_->setBounds(0, 0, 0, 0);
        return;
    }

    // Toolbar at top
    auto toolbarArea = bounds.removeFromTop(TOOLBAR_HEIGHT);
    timeModeButton_->setBounds(toolbarArea.removeFromLeft(60).reduced(2));
    toolbarArea.removeFromLeft(4);
    warpModeButton_->setBounds(toolbarArea.removeFromLeft(60).reduced(2));
    toolbarArea.removeFromLeft(4);
    gridResolutionCombo_->setBounds(toolbarArea.removeFromLeft(70).reduced(2));
    toolbarArea.removeFromLeft(4);
    loopGhostButton_->setBounds(toolbarArea.removeFromLeft(60).reduced(2));
    toolbarArea.removeFromLeft(4);
    bpmLabel_->setBounds(toolbarArea.removeFromLeft(80).reduced(2));

    // Time ruler below toolbar
    auto rulerArea = bounds.removeFromTop(TIME_RULER_HEIGHT);
    timeRuler_->setBounds(rulerArea);

    // Viewport fills remaining space
    viewport_->setBounds(bounds);

    // Playhead overlay covers the viewport area
    if (playheadOverlay_) {
        playheadOverlay_->setBounds(bounds);
    }

    // Set grid minimum height to match viewport's visible height so waveform fills the space
    if (gridComponent_) {
        gridComponent_->setMinimumHeight(viewport_->getMaximumVisibleHeight());
    }

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

void WaveformEditorContent::mouseDown(const juce::MouseEvent& event) {
    bool overHeader = event.y < (TOOLBAR_HEIGHT + TIME_RULER_HEIGHT);
    if (overHeader) {
        headerDragActive_ = true;
        headerDragStartX_ = event.x;
        headerDragStartZoom_ = horizontalZoom_;
    }
}

void WaveformEditorContent::mouseDrag(const juce::MouseEvent& event) {
    if (headerDragActive_) {
        int deltaX = event.x - headerDragStartX_;
        // Drag right = zoom in, drag left = zoom out
        // ~200px drag = 2x zoom change
        double zoomFactor = std::pow(2.0, deltaX / 200.0);
        double newZoom = headerDragStartZoom_ * zoomFactor;
        newZoom = juce::jlimit(MIN_ZOOM, MAX_ZOOM, newZoom);

        if (newZoom != horizontalZoom_) {
            horizontalZoom_ = newZoom;
            gridComponent_->setHorizontalZoom(horizontalZoom_);

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
        }
    }
}

void WaveformEditorContent::mouseUp(const juce::MouseEvent& /*event*/) {
    headerDragActive_ = false;
}

void WaveformEditorContent::mouseMove(const juce::MouseEvent& event) {
    bool overHeader = event.y < (TOOLBAR_HEIGHT + TIME_RULER_HEIGHT);
    if (overHeader) {
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    } else {
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }
}

void WaveformEditorContent::mouseWheelMove(const juce::MouseEvent& event,
                                           const juce::MouseWheelDetails& wheel) {
    // Check if mouse is over the toolbar or time ruler area (header)
    bool overHeader = event.y < (TOOLBAR_HEIGHT + TIME_RULER_HEIGHT);

    if (overHeader || event.mods.isCommandDown()) {
        // Scroll on header OR Cmd+scroll anywhere = horizontal zoom
        double zoomFactor = 1.0 + (wheel.deltaY * 0.5);
        int anchorX = event.x - viewport_->getX();
        performAnchorPointZoom(zoomFactor, anchorX);
    } else if (event.mods.isAltDown()) {
        // Alt + scroll anywhere = vertical zoom
        double zoomFactor = 1.0 + (wheel.deltaY * 0.5);
        double newZoom = verticalZoom_ * zoomFactor;
        newZoom = juce::jlimit(MIN_VERTICAL_ZOOM, MAX_VERTICAL_ZOOM, newZoom);
        if (newZoom != verticalZoom_) {
            verticalZoom_ = newZoom;
            gridComponent_->setVerticalZoom(verticalZoom_);
        }
    } else {
        // Normal scroll over waveform area - let viewport handle it
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
        const auto* clip = magda::ClipManager::getInstance().getClip(clipId);
        if (clip) {
            // Update grid component's clip position (lightweight, no full reload)
            // This is needed when clip is moved from the timeline
            gridComponent_->updateClipPosition(clip->startTime, clip->length);

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

            // Update loop boundary dimming
            updateDisplayInfo(*clip);

            // Update warp mode state
            bool warpEnabled = clip->warpEnabled;
            warpModeButton_->setToggleState(warpEnabled, juce::dontSendNotification);
            gridComponent_->setWarpMode(warpEnabled);

            if (warpEnabled) {
                auto* bridge = getBridge();
                if (bridge) {
                    // Only refresh markers when transitioning to warp mode
                    // (not on every clip property change to avoid performance issues)
                    if (!wasWarpEnabled_) {
                        bridge->enableWarp(editingClipId_);
                        auto markers = bridge->getWarpMarkers(editingClipId_);
                        gridComponent_->setWarpMarkers(markers);
                    }
                }
            } else if (wasWarpEnabled_) {
                // Only disable if warp was previously on
                auto* bridge = getBridge();
                if (bridge) {
                    bridge->disableWarp(editingClipId_);
                }
            }
            wasWarpEnabled_ = warpEnabled;

            // Update BPM label
            {
                double detectedBPM =
                    magda::AudioThumbnailManager::getInstance().detectBPM(clip->audioFilePath);
                if (detectedBPM > 0.0) {
                    bpmLabel_->setText(juce::String(detectedBPM, 1) + " BPM",
                                       juce::dontSendNotification);
                    bpmLabel_->setColour(juce::Label::textColourId, DarkTheme::getTextColour());
                } else {
                    bpmLabel_->setText(juce::String::fromUTF8("\xe2\x80\x94 BPM"),
                                       juce::dontSendNotification);
                    bpmLabel_->setColour(juce::Label::textColourId,
                                         DarkTheme::getSecondaryTextColour());
                }
            }

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
// TimelineStateListener
// ============================================================================

void WaveformEditorContent::timelineStateChanged(const TimelineState& state) {
    // Sync tempo to the TimeRuler so beat grid and snap stay in sync
    timeRuler_->setTempo(state.tempo.bpm);
    timeRuler_->setTimeSignature(state.tempo.timeSignatureNumerator,
                                 state.tempo.timeSignatureDenominator);
    gridComponent_->repaint();
}

void WaveformEditorContent::playheadStateChanged(const TimelineState& state) {
    cachedEditPosition_ = state.playhead.editPosition;
    cachedPlaybackPosition_ = state.playhead.playbackPosition;
    cachedIsPlaying_ = state.playhead.isPlaying;

    if (playheadOverlay_) {
        playheadOverlay_->repaint();
    }
}

// ============================================================================
// Public Methods
// ============================================================================

void WaveformEditorContent::setClip(magda::ClipId clipId) {
    if (editingClipId_ != clipId) {
        editingClipId_ = clipId;
        transientsCached_ = false;
        stopTimer();
        gridComponent_->setClip(clipId);

        // Update time ruler with clip info
        const auto* clip = magda::ClipManager::getInstance().getClip(clipId);
        if (clip) {
            // Auto-switch time mode based on clip view
            // Session clips are locked to relative mode (no absolute timeline position)
            bool isSessionClip = (clip->view == magda::ClipView::Session);
            if (isSessionClip) {
                setRelativeTimeMode(true);
                timeModeButton_->setEnabled(false);
                timeModeButton_->setTooltip("Session clips always use relative time");
            } else {
                timeModeButton_->setEnabled(true);
                timeModeButton_->setTooltip(
                    "Toggle between Absolute (timeline) and Relative (clip) mode");
            }

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

            updateDisplayInfo(*clip);

            // Update BPM label
            {
                double detectedBPM =
                    magda::AudioThumbnailManager::getInstance().detectBPM(clip->audioFilePath);
                if (detectedBPM > 0.0) {
                    bpmLabel_->setText(juce::String(detectedBPM, 1) + " BPM",
                                       juce::dontSendNotification);
                    bpmLabel_->setColour(juce::Label::textColourId, DarkTheme::getTextColour());
                } else {
                    bpmLabel_->setText(juce::String::fromUTF8("\xe2\x80\x94 BPM"),
                                       juce::dontSendNotification);
                    bpmLabel_->setColour(juce::Label::textColourId,
                                         DarkTheme::getSecondaryTextColour());
                }
            }
        }

        // Update warp mode state
        if (clip) {
            bool warpEnabled = clip->warpEnabled;
            warpModeButton_->setToggleState(warpEnabled, juce::dontSendNotification);
            gridComponent_->setWarpMode(warpEnabled);
            wasWarpEnabled_ = warpEnabled;

            if (warpEnabled) {
                auto* bridge = getBridge();
                if (bridge) {
                    // Always populate markers when opening a clip with warp enabled
                    bridge->enableWarp(editingClipId_);
                    auto markers = bridge->getWarpMarkers(editingClipId_);
                    gridComponent_->setWarpMarkers(markers);
                }
            }
        }

        // Check for cached transients or start polling
        if (clip && clip->type == magda::ClipType::Audio && !clip->audioFilePath.isEmpty()) {
            auto* cached = magda::AudioThumbnailManager::getInstance().getCachedTransients(
                clip->audioFilePath);
            if (cached) {
                gridComponent_->setTransientTimes(*cached);
                transientsCached_ = true;
            } else {
                startTimer(250);
            }
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

void WaveformEditorContent::updateDisplayInfo(const magda::ClipInfo& clip) {
    double bpm = 120.0;
    auto* controller = magda::TimelineController::getCurrent();
    if (controller) {
        bpm = controller->getState().tempo.bpm;
    }

    // Get file duration for source extent calculation
    double fileDuration = 0.0;
    if (clip.audioFilePath.isNotEmpty()) {
        auto* thumbnail =
            magda::AudioThumbnailManager::getInstance().getThumbnail(clip.audioFilePath);
        if (thumbnail) {
            fileDuration = thumbnail->getTotalLength();
        }
    }

    auto info = magda::ClipDisplayInfo::from(clip, bpm, fileDuration);
    gridComponent_->setDisplayInfo(info);

    // Update time ruler loop region (green markers with triangles)
    // In loop mode, display is anchored at loopStart, so loop starts at position 0
    if (timeRuler_) {
        bool showMarkers = clip.loopLength > 0.0;
        double loopStartPos = info.loopStartPositionSeconds;
        double loopLen = info.loopLengthSeconds;
        timeRuler_->setLoopRegion(loopStartPos, loopLen, showMarkers);

        // Show offset/phase marker when offset differs from loopStart
        bool showPhase = info.loopEnabled && std::abs(info.offsetPositionSeconds) > 0.001;
        timeRuler_->setLoopPhaseMarker(info.offsetPositionSeconds, showPhase);
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

// ============================================================================
// Warp Helpers
// ============================================================================

void WaveformEditorContent::refreshWarpMarkers() {
    auto* bridge = getBridge();
    if (bridge && editingClipId_ != magda::INVALID_CLIP_ID) {
        auto markers = bridge->getWarpMarkers(editingClipId_);
        gridComponent_->setWarpMarkers(markers);
    }
}

magda::AudioBridge* WaveformEditorContent::getBridge() {
    auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
    if (!audioEngine)
        return nullptr;
    return audioEngine->getAudioBridge();
}

// ============================================================================
// Timer (Transient Detection Polling)
// ============================================================================

void WaveformEditorContent::timerCallback() {
    if (transientsCached_ || editingClipId_ == magda::INVALID_CLIP_ID) {
        stopTimer();
        return;
    }

    auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
    if (!audioEngine)
        return;

    auto* bridge = audioEngine->getAudioBridge();
    if (!bridge)
        return;

    if (bridge->getTransientTimes(editingClipId_)) {
        const auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
        if (clip && !clip->audioFilePath.isEmpty()) {
            auto* cached = magda::AudioThumbnailManager::getInstance().getCachedTransients(
                clip->audioFilePath);
            if (cached) {
                gridComponent_->setTransientTimes(*cached);
            }
        }
        transientsCached_ = true;
        stopTimer();
    }
}

}  // namespace magda::daw::ui
