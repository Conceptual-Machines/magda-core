#include "WaveformEditorContent.hpp"

#include <cmath>

#include "../../state/TimelineController.hpp"
#include "../../themes/CursorManager.hpp"
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

        const auto& di = owner_.cachedDisplayInfo_;

        // The editor shows source file content — convert arrangement time
        // to source-file position. Only show cursors when the arrangement
        // playhead falls within this clip's time range.
        double clipEnd = clip->startTime + clip->length;

        auto arrangementToSourceX = [&](double arrangementTime) -> int {
            // Map arrangement time to position within source file
            double relTime = arrangementTime - clip->startTime;
            double sourcePos = di.offsetPositionSeconds + relTime;
            return static_cast<int>(sourcePos * owner_.horizontalZoom_) + GRID_LEFT_PADDING -
                   scrollX;
        };

        // Draw edit cursor (triangle at top) — only when inside clip range
        double editPos = owner_.cachedEditPosition_;
        if (editPos >= clip->startTime && editPos <= clipEnd) {
            int editX = arrangementToSourceX(editPos);
            if (editX >= 0 && editX < getWidth()) {
                g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_RED));
                juce::Path triangle;
                triangle.addTriangle(static_cast<float>(editX - 5), 0.0f,
                                     static_cast<float>(editX + 5), 0.0f, static_cast<float>(editX),
                                     10.0f);
                g.fillPath(triangle);
            }
        }

        // Draw playback cursor (vertical line) — only during playback and inside clip
        if (owner_.cachedIsPlaying_) {
            double playPos = owner_.cachedPlaybackPosition_;

            // Only show when playhead is within clip's arrangement range
            if (playPos < clip->startTime || playPos > clipEnd)
                return;

            // Wrap playhead inside loop region when looping is enabled
            if (clip->loopEnabled && di.loopLengthSeconds > 0.0) {
                double relPos = playPos - clip->startTime;
                double phaseShift = di.offsetPositionSeconds - di.loopStartPositionSeconds;
                double wrapped = std::fmod(phaseShift + relPos, di.loopLengthSeconds);
                if (wrapped < 0.0)
                    wrapped += di.loopLengthSeconds;
                double displayPos = di.loopStartPositionSeconds + wrapped;
                int playX = static_cast<int>(displayPos * owner_.horizontalZoom_) +
                            GRID_LEFT_PADDING - scrollX;
                if (playX >= 0 && playX < getWidth()) {
                    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_RED));
                    g.drawLine(static_cast<float>(playX), 0.0f, static_cast<float>(playX),
                               static_cast<float>(getHeight()), 1.5f);
                }
                return;
            }

            int playX = arrangementToSourceX(playPos);
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

    // Grid resolution num/den controls (like transport header)
    auto applyGridBeats = [this]() {
        if (gridNumerator_ <= 0) {
            gridComponent_->setGridResolutionBeats(0.0);
        } else {
            double beats = static_cast<double>(gridNumerator_) * (4.0 / gridDenominator_);
            gridComponent_->setGridResolutionBeats(beats);
        }
    };

    gridNumeratorLabel_ =
        std::make_unique<magda::DraggableValueLabel>(magda::DraggableValueLabel::Format::Integer);
    gridNumeratorLabel_->setRange(0.0, 128.0, 1.0);
    gridNumeratorLabel_->setValue(static_cast<double>(gridNumerator_), juce::dontSendNotification);
    gridNumeratorLabel_->setTextColour(DarkTheme::getSecondaryTextColour());
    gridNumeratorLabel_->setShowFillIndicator(false);
    gridNumeratorLabel_->setFontSize(11.0f);
    gridNumeratorLabel_->setDoubleClickResetsValue(true);
    gridNumeratorLabel_->setDrawBorder(false);
    gridNumeratorLabel_->onValueChange = [this, applyGridBeats]() {
        gridNumerator_ = static_cast<int>(gridNumeratorLabel_->getValue());
        applyGridBeats();
    };
    addAndMakeVisible(gridNumeratorLabel_.get());

    gridSlashLabel_ = std::make_unique<juce::Label>("gridSlash", "/");
    gridSlashLabel_->setFont(magda::FontManager::getInstance().getUIFont(11.0f));
    gridSlashLabel_->setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    gridSlashLabel_->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(gridSlashLabel_.get());

    gridDenominatorLabel_ =
        std::make_unique<magda::DraggableValueLabel>(magda::DraggableValueLabel::Format::Integer);
    gridDenominatorLabel_->setRange(1.0, 64.0, 4.0);
    gridDenominatorLabel_->setValue(static_cast<double>(gridDenominator_),
                                    juce::dontSendNotification);
    gridDenominatorLabel_->setTextColour(DarkTheme::getSecondaryTextColour());
    gridDenominatorLabel_->setShowFillIndicator(false);
    gridDenominatorLabel_->setFontSize(11.0f);
    gridDenominatorLabel_->setDoubleClickResetsValue(true);
    gridDenominatorLabel_->setDrawBorder(false);
    gridDenominatorLabel_->onValueChange = [this, applyGridBeats]() {
        int raw = static_cast<int>(gridDenominatorLabel_->getValue());
        int pow2 = 1;
        while (pow2 * 2 <= raw)
            pow2 *= 2;
        if (raw - pow2 > pow2 * 2 - raw && pow2 * 2 <= 64)
            pow2 *= 2;
        gridDenominator_ = pow2;
        gridDenominatorLabel_->setValue(static_cast<double>(gridDenominator_),
                                        juce::dontSendNotification);
        applyGridBeats();
    };
    addAndMakeVisible(gridDenominatorLabel_.get());

    // Snap toggle button
    snapButton_ = std::make_unique<juce::TextButton>("SNAP");
    snapButton_->setClickingTogglesState(true);
    snapButton_->setToggleState(false, juce::dontSendNotification);
    snapButton_->setLookAndFeel(buttonLookAndFeel_.get());
    snapButton_->onClick = [this]() {
        gridComponent_->setSnapEnabled(snapButton_->getToggleState());
    };
    addAndMakeVisible(snapButton_.get());

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

    // Zoom drag on waveform — same log-curve sensitivity as header drag
    gridComponent_->onZoomDrag = [this](int deltaY, int anchorX) {
        // deltaY == 0 signals drag start — capture starting zoom
        if (deltaY == 0) {
            waveformZoomStartZoom_ = horizontalZoom_;
            return;
        }

        double startZoom = waveformZoomStartZoom_;
        if (startZoom <= 0.0)
            startZoom = horizontalZoom_;

        double zoomRange = std::log(MAX_ZOOM) - std::log(MIN_ZOOM);
        double zoomPosition = (std::log(startZoom) - std::log(MIN_ZOOM)) / zoomRange;

        double minSensitivity = 25.0;
        double maxSensitivity = 40.0;
        double baseSensitivity = minSensitivity + zoomPosition * (maxSensitivity - minSensitivity);

        double sensitivity = baseSensitivity;

        double absDeltaY = std::abs(static_cast<double>(deltaY));
        if (absDeltaY > 80.0) {
            double accelerationFactor = 1.0 + (absDeltaY - 80.0) / 150.0;
            sensitivity /= accelerationFactor;
        }

        double exponent = static_cast<double>(deltaY) / sensitivity;
        double newZoom = startZoom * std::pow(2.0, exponent);
        newZoom = juce::jlimit(MIN_ZOOM, MAX_ZOOM, newZoom);

        if (newZoom != horizontalZoom_) {
            // anchorX is already viewport-relative (converted in grid mouseDown)
            performAnchorPointZoom(newZoom / horizontalZoom_, anchorX);
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
    if (timeModeButton_)
        timeModeButton_->setLookAndFeel(nullptr);
    if (snapButton_)
        snapButton_->setLookAndFeel(nullptr);
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
        gridNumeratorLabel_->setBounds(0, 0, 0, 0);
        gridSlashLabel_->setBounds(0, 0, 0, 0);
        gridDenominatorLabel_->setBounds(0, 0, 0, 0);
        snapButton_->setBounds(0, 0, 0, 0);
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
    gridNumeratorLabel_->setBounds(toolbarArea.removeFromLeft(28).reduced(2));
    gridSlashLabel_->setBounds(toolbarArea.removeFromLeft(10).reduced(0, 2));
    gridDenominatorLabel_->setBounds(toolbarArea.removeFromLeft(28).reduced(2));
    toolbarArea.removeFromLeft(4);
    snapButton_->setBounds(toolbarArea.removeFromLeft(44).reduced(2));

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
        headerDragStartY_ = event.y;
        headerDragAnchorX_ = event.x - viewport_->getX();
        headerDragStartZoom_ = horizontalZoom_;
    }
}

void WaveformEditorContent::mouseDrag(const juce::MouseEvent& event) {
    if (headerDragActive_) {
        // Vertical drag: up = zoom in, down = zoom out (matches arranger)
        int deltaY = headerDragStartY_ - event.y;

        // Zoom-level-dependent sensitivity (log curve):
        // When zoomed out → lower sensitivity (faster zoom)
        // When zoomed in → higher sensitivity (finer control)
        double zoomRange = std::log(MAX_ZOOM) - std::log(MIN_ZOOM);
        double zoomPosition = (std::log(headerDragStartZoom_) - std::log(MIN_ZOOM)) / zoomRange;

        double minSensitivity = 25.0;  // Fast when zoomed out
        double maxSensitivity = 40.0;  // Finer when zoomed in
        double baseSensitivity = minSensitivity + zoomPosition * (maxSensitivity - minSensitivity);

        double sensitivity = baseSensitivity;
        if (event.mods.isShiftDown()) {
            sensitivity = 8.0;  // Turbo
        } else if (event.mods.isAltDown()) {
            sensitivity = baseSensitivity * 3.0;  // Fine
        }

        // Progressive acceleration after 80px of drag
        double absDeltaY = std::abs(static_cast<double>(deltaY));
        if (absDeltaY > 80.0) {
            double accelerationFactor = 1.0 + (absDeltaY - 80.0) / 150.0;
            sensitivity /= accelerationFactor;
        }

        double exponent = static_cast<double>(deltaY) / sensitivity;
        double newZoom = headerDragStartZoom_ * std::pow(2.0, exponent);
        newZoom = juce::jlimit(MIN_ZOOM, MAX_ZOOM, newZoom);

        if (newZoom != horizontalZoom_) {
            if (deltaY > 0) {
                setMouseCursor(magda::CursorManager::getInstance().getZoomInCursor());
            } else if (deltaY < 0) {
                setMouseCursor(magda::CursorManager::getInstance().getZoomOutCursor());
            }

            performAnchorPointZoom(newZoom / horizontalZoom_, headerDragAnchorX_);
        }
    }
}

void WaveformEditorContent::mouseUp(const juce::MouseEvent& /*event*/) {
    headerDragActive_ = false;
}

void WaveformEditorContent::mouseMove(const juce::MouseEvent& event) {
    bool overHeader = event.y < (TOOLBAR_HEIGHT + TIME_RULER_HEIGHT);
    if (overHeader) {
        setMouseCursor(magda::CursorManager::getInstance().getZoomCursor());
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
            // The editor is a source file viewer — ruler stays locked to
            // source-relative mode.  Update clip boundaries (needed for resize)
            // and display info (offset marker, loop markers).
            gridComponent_->updateClipPosition(clip->startTime, clip->length);
            timeRuler_->setClipLength(clip->length);
            updateDisplayInfo(*clip);

            // Update warp mode state
            bool warpEnabled = clip->warpEnabled;
            gridComponent_->setWarpMode(warpEnabled);

            if (warpEnabled) {
                auto* bridge = getBridge();
                if (bridge) {
                    if (!wasWarpEnabled_) {
                        bridge->enableWarp(editingClipId_);
                        auto markers = bridge->getWarpMarkers(editingClipId_);
                        gridComponent_->setWarpMarkers(markers);
                    }
                }
            } else if (wasWarpEnabled_) {
                auto* bridge = getBridge();
                if (bridge) {
                    bridge->disableWarp(editingClipId_);
                }
            }
            wasWarpEnabled_ = warpEnabled;
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

void WaveformEditorContent::timelineStateChanged(const TimelineState& state, ChangeFlags changes) {
    // Playhead changes
    if (hasFlag(changes, ChangeFlags::Playhead)) {
        cachedEditPosition_ = state.playhead.editPosition;
        cachedPlaybackPosition_ = state.playhead.playbackPosition;
        cachedIsPlaying_ = state.playhead.isPlaying;

        if (playheadOverlay_) {
            playheadOverlay_->repaint();
        }
    }

    // Tempo changes - BPM zoom scaling + ruler sync
    if (hasFlag(changes, ChangeFlags::Tempo)) {
        double newBpm = state.tempo.bpm;

        // Scale pps zoom to keep visual bar width constant when BPM changes.
        // new_pps = old_pps * new_bpm / old_bpm  (keeps ppb constant)
        if (cachedBpm_ > 0.0 && std::abs(newBpm - cachedBpm_) > 0.01) {
            horizontalZoom_ =
                juce::jlimit(MIN_ZOOM, MAX_ZOOM, horizontalZoom_ * newBpm / cachedBpm_);
            gridComponent_->setHorizontalZoom(horizontalZoom_);
            timeRuler_->setZoom(horizontalZoom_ * 60.0 / newBpm);
            updateGridSize();
        }
        cachedBpm_ = newBpm;

        // Sync tempo to the TimeRuler so beat grid and snap stay in sync
        timeRuler_->setTempo(newBpm);
        timeRuler_->setTimeSignature(state.tempo.timeSignatureNumerator,
                                     state.tempo.timeSignatureDenominator);
        gridComponent_->repaint();
    }
}

// ============================================================================
// Public Methods
// ============================================================================

void WaveformEditorContent::setClip(magda::ClipId clipId) {
    if (editingClipId_ != clipId) {
        editingClipId_ = clipId;
        transientsCached_ = false;
        transientPollCount_ = 0;
        stopTimer();
        gridComponent_->setClip(clipId);

        // Update time ruler with clip info
        const auto* clip = magda::ClipManager::getInstance().getClip(clipId);
        if (clip) {
            // Audio clips always use relative mode — the editor shows source file
            // content, not timeline position. The ruler is anchored to the source file.
            setRelativeTimeMode(true);
            timeModeButton_->setEnabled(false);
            timeModeButton_->setVisible(false);

            // Get tempo from TimelineController
            double bpm = 120.0;
            auto* controller = magda::TimelineController::getCurrent();
            if (controller) {
                bpm = controller->getState().tempo.bpm;
            }
            cachedBpm_ = bpm;

            timeRuler_->setZoom(horizontalZoom_ * 60.0 / bpm);
            timeRuler_->setTempo(bpm);
            timeRuler_->setTimeOffset(0.0);
            timeRuler_->setClipLength(clip->length);

            updateDisplayInfo(*clip);
        }

        // Update warp mode state
        if (clip) {
            bool warpEnabled = clip->warpEnabled;
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

        // Update time ruler offset for the new mode
        const auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
        if (clip && timeRuler_) {
            timeRuler_->setTimeOffset(relative ? 0.0 : clip->startTime);
        }

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
            double totalTime;
            if (relativeTimeMode_) {
                // In relative mode, ruler spans the full source file duration
                double fileDuration = 0.0;
                if (clip->audioFilePath.isNotEmpty()) {
                    auto* thumbnail = magda::AudioThumbnailManager::getInstance().getThumbnail(
                        clip->audioFilePath);
                    if (thumbnail) {
                        fileDuration = thumbnail->getTotalLength();
                    }
                }
                // Convert file duration to timeline seconds (accounting for speed/tempo)
                double bpm = cachedBpm_ > 0.0 ? cachedBpm_ : 120.0;
                auto info = magda::ClipDisplayInfo::from(*clip, bpm, fileDuration);
                totalTime = info.fullSourceExtentSeconds;
            } else {
                totalTime = clip->startTime + clip->length;
            }
            timeRuler_->setTimelineLength(totalTime + 10.0);  // Add padding
        }
    }
}

void WaveformEditorContent::scrollToClipStart() {
    if (relativeTimeMode_) {
        // In relative mode, scroll to the offset position so user sees the relevant audio
        const auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
        if (clip && gridComponent_) {
            double bpm = cachedBpm_ > 0.0 ? cachedBpm_ : 120.0;
            double fileDuration = 0.0;
            if (clip->audioFilePath.isNotEmpty()) {
                auto* thumbnail =
                    magda::AudioThumbnailManager::getInstance().getThumbnail(clip->audioFilePath);
                if (thumbnail) {
                    fileDuration = thumbnail->getTotalLength();
                }
            }
            auto info = magda::ClipDisplayInfo::from(*clip, bpm, fileDuration);
            int offsetX = gridComponent_->timeToPixel(info.offsetPositionSeconds);
            // Center the offset in the viewport
            int viewportWidth = viewport_->getWidth();
            int scrollX = juce::jmax(0, offsetX - viewportWidth / 4);
            viewport_->setViewPosition(scrollX, viewport_->getViewPositionY());
        } else {
            viewport_->setViewPosition(0, viewport_->getViewPositionY());
        }
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
    cachedDisplayInfo_ = info;
    gridComponent_->setDisplayInfo(info);

    // Update time ruler loop region (green when active, grey when disabled)
    // Display anchored at file start — loop markers at real source positions
    if (timeRuler_) {
        bool showMarkers = clip.loopEnabled && clip.loopLength > 0.0;
        bool loopIsActive = clip.loopEnabled;
        double loopStartPos = info.loopStartPositionSeconds;
        double loopLen = info.loopLengthSeconds;
        timeRuler_->setLoopRegion(loopStartPos, loopLen, showMarkers, loopIsActive);

        // Shift clip boundary markers by the content offset
        timeRuler_->setClipContentOffset(info.offsetPositionSeconds);

        // Offset marker is shown on the waveform grid; no separate ruler marker needed
        timeRuler_->setLoopPhaseMarker(0.0, false);
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
            timeRuler_->setZoom(horizontalZoom_ * 60.0 / bpm);
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
// TODO: Expose transient detection sensitivity control. TE's TransientDetectionJob
//       accepts a Config { float sensitivity } but WarpTimeManager hardcodes 0.5f.
//       Would need to modify WarpTimeManager or re-run the job with a custom config.
// ============================================================================

void WaveformEditorContent::timerCallback() {
    if (transientsCached_ || editingClipId_ == magda::INVALID_CLIP_ID) {
        stopTimer();
        return;
    }

    if (++transientPollCount_ >= MAX_TRANSIENT_POLL_ATTEMPTS) {
        DBG("WaveformEditorContent: transient detection timed out after "
            << MAX_TRANSIENT_POLL_ATTEMPTS << " attempts");
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
