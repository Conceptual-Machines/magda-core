#include "MidiEditorContent.hpp"

#include "ui/components/timeline/TimeRuler.hpp"
#include "ui/state/TimelineController.hpp"
#include "ui/state/TimelineEvents.hpp"
#include "ui/state/TimelineState.hpp"

namespace magda::daw::ui {

MidiEditorContent::MidiEditorContent() {
    // Create time ruler
    timeRuler_ = std::make_unique<magda::TimeRuler>();
    timeRuler_->setDisplayMode(magda::TimeRuler::DisplayMode::BarsBeats);
    timeRuler_->setLeftPadding(GRID_LEFT_PADDING);
    timeRuler_->setRelativeMode(relativeTimeMode_);
    addAndMakeVisible(timeRuler_.get());

    // Create viewport
    viewport_ = std::make_unique<MidiEditorViewport>();
    viewport_->onScrolled = [this](int x, int y) {
        timeRuler_->setScrollOffset(x);
        onScrollPositionChanged(x, y);
    };
    viewport_->componentsToRepaint.push_back(timeRuler_.get());
    viewport_->setScrollBarsShown(true, true);
    addAndMakeVisible(viewport_.get());

    // Link TimeRuler to viewport for real-time scroll sync
    timeRuler_->setLinkedViewport(viewport_.get());

    // TimeRuler zoom callback (drag up/down to zoom)
    timeRuler_->onZoomChanged = [this](double newZoom, double anchorTime, int anchorScreenX) {
        performAnchorPointZoom(newZoom, anchorTime, anchorScreenX);
    };

    // TimeRuler scroll callback (drag left/right to scroll)
    timeRuler_->onScrollRequested = [this](int deltaX) {
        int newScrollX = viewport_->getViewPositionX() + deltaX;
        newScrollX = juce::jmax(0, newScrollX);
        viewport_->setViewPosition(newScrollX, viewport_->getViewPositionY());
    };

    // TimeRuler click callback — set edit cursor position
    timeRuler_->onPositionClicked = [](double time) {
        if (auto* controller = magda::TimelineController::getCurrent()) {
            controller->dispatch(magda::SetEditCursorEvent{time});
        }
    };

    // Edit cursor blink timer
    blinkTimer_.callback = [this]() {
        editCursorBlinkVisible_ = !editCursorBlinkVisible_;

        if (auto* controller = magda::TimelineController::getCurrent()) {
            double editPos = controller->getState().editCursorPosition;
            bool visible = editPos >= 0.0;
            setGridEditCursorPosition(editPos, visible && editCursorBlinkVisible_);
            if (timeRuler_) {
                timeRuler_->setEditCursorPosition(editPos, editCursorBlinkVisible_);
            }
        }
    };

    // Register as ClipManager listener
    magda::ClipManager::getInstance().addListener(this);

    // Register as TimelineController listener for playhead updates
    if (auto* controller = magda::TimelineController::getCurrent()) {
        controller->addListener(this);
    }

    // Check for already-selected MIDI clip (subclass constructors complete setup)
    magda::ClipId selectedClip = magda::ClipManager::getInstance().getSelectedClip();
    if (selectedClip != magda::INVALID_CLIP_ID) {
        const auto* clip = magda::ClipManager::getInstance().getClip(selectedClip);
        if (clip && clip->type == magda::ClipType::MIDI) {
            editingClipId_ = selectedClip;
        }
    }

    // Initialize grid from clip settings (or auto-compute from zoom)
    applyClipGridSettings();
}

MidiEditorContent::~MidiEditorContent() {
    blinkTimer_.stopTimer();
    magda::ClipManager::getInstance().removeListener(this);

    if (auto* controller = magda::TimelineController::getCurrent()) {
        controller->removeListener(this);
    }
}

// ============================================================================
// Zoom
// ============================================================================

void MidiEditorContent::performAnchorPointZoom(double newZoom, double anchorTime,
                                               int anchorScreenX) {
    double tempo = 120.0;
    if (auto* controller = magda::TimelineController::getCurrent()) {
        tempo = controller->getState().tempo.bpm;
    }
    double secondsPerBeat = 60.0 / tempo;

    double newPixelsPerBeat = juce::jlimit(MIN_HORIZONTAL_ZOOM, MAX_HORIZONTAL_ZOOM, newZoom);

    if (newPixelsPerBeat != horizontalZoom_) {
        double anchorBeat = anchorTime / secondsPerBeat;

        horizontalZoom_ = newPixelsPerBeat;
        setGridPixelsPerBeat(horizontalZoom_);
        updateGridResolution();
        updateGridSize();
        updateTimeRuler();
        // Note: do NOT push auto grid display to shared timeline state —
        // the MIDI editor has its own independent zoom/grid.

        // Adjust scroll to keep anchor position under mouse
        int newAnchorX = static_cast<int>(anchorBeat * horizontalZoom_) + GRID_LEFT_PADDING;
        int newScrollX = newAnchorX - anchorScreenX;
        newScrollX = juce::jmax(0, newScrollX);
        viewport_->setViewPosition(newScrollX, viewport_->getViewPositionY());
    }
}

void MidiEditorContent::performWheelZoom(double zoomFactor, int mouseXInViewport) {
    int mouseXInContent = mouseXInViewport + viewport_->getViewPositionX();
    double anchorBeat = static_cast<double>(mouseXInContent - GRID_LEFT_PADDING) / horizontalZoom_;

    double newZoom = horizontalZoom_ * zoomFactor;
    newZoom = juce::jlimit(MIN_HORIZONTAL_ZOOM, MAX_HORIZONTAL_ZOOM, newZoom);

    if (newZoom != horizontalZoom_) {
        horizontalZoom_ = newZoom;
        setGridPixelsPerBeat(horizontalZoom_);
        updateGridResolution();
        updateGridSize();
        updateTimeRuler();
        // Note: do NOT push auto grid display to shared timeline state —
        // the MIDI editor has its own independent zoom/grid.

        // Adjust scroll position to keep anchor point under mouse
        int newAnchorX = static_cast<int>(anchorBeat * horizontalZoom_) + GRID_LEFT_PADDING;
        int newScrollX = newAnchorX - mouseXInViewport;
        newScrollX = juce::jmax(0, newScrollX);
        viewport_->setViewPosition(newScrollX, viewport_->getViewPositionY());
    }
}

// ============================================================================
// TimeRuler
// ============================================================================

void MidiEditorContent::updateTimeRuler() {
    if (!timeRuler_)
        return;

    const auto* clip = editingClipId_ != magda::INVALID_CLIP_ID
                           ? magda::ClipManager::getInstance().getClip(editingClipId_)
                           : nullptr;

    // Get tempo from TimelineController
    double tempo = 120.0;
    if (auto* controller = magda::TimelineController::getCurrent()) {
        const auto& state = controller->getState();
        tempo = state.tempo.bpm;
        timeRuler_->setTimeSignature(state.tempo.timeSignatureNumerator,
                                     state.tempo.timeSignatureDenominator);
    }
    timeRuler_->setTempo(tempo);

    // Get timeline length
    double timelineLength = 300.0;
    if (auto* controller = magda::TimelineController::getCurrent()) {
        timelineLength = controller->getState().timelineLength;
    }
    timeRuler_->setTimelineLength(timelineLength);

    // Set zoom and grid resolution (pixels per beat)
    timeRuler_->setZoom(horizontalZoom_);
    timeRuler_->setGridResolution(gridResolutionBeats_);

    // Set clip info for boundary drawing
    if (clip) {
        if (clip->view == magda::ClipView::Session) {
            timeRuler_->setTimeOffset(0.0);
            timeRuler_->setClipLength(clip->length);
        } else {
            timeRuler_->setTimeOffset(clip->startTime);
            timeRuler_->setClipLength(clip->length);
        }
    } else {
        timeRuler_->setTimeOffset(0.0);
        timeRuler_->setClipLength(0.0);
    }

    // Update relative mode
    timeRuler_->setRelativeMode(relativeTimeMode_);

    // Set loop region markers
    if (clip) {
        timeRuler_->setLoopRegion(clip->offset - clip->loopStart, clip->loopLength,
                                  clip->loopEnabled);
    } else {
        timeRuler_->setLoopRegion(0.0, 0.0, false);
    }
}

// ============================================================================
// Relative time mode
// ============================================================================

void MidiEditorContent::setRelativeTimeMode(bool relative) {
    if (relativeTimeMode_ != relative) {
        relativeTimeMode_ = relative;
        updateGridSize();
        updateTimeRuler();
        repaint();
    }
}

// ============================================================================
// ClipManagerListener defaults
// ============================================================================

void MidiEditorContent::clipsChanged() {
    if (editingClipId_ != magda::INVALID_CLIP_ID) {
        const auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
        if (!clip) {
            editingClipId_ = magda::INVALID_CLIP_ID;
        }
    }
    updateGridSize();
    updateTimeRuler();
    repaint();
}

void MidiEditorContent::clipPropertyChanged(magda::ClipId clipId) {
    if (clipId == editingClipId_) {
        juce::Component::SafePointer<MidiEditorContent> safeThis(this);
        juce::MessageManager::callAsync([safeThis]() {
            if (auto* self = safeThis.getComponent()) {
                self->applyClipGridSettings();
                self->updateGridSize();
                self->updateTimeRuler();
                self->repaint();
            }
        });
    }
}

// ============================================================================
// TimelineStateListener
// ============================================================================

void MidiEditorContent::timelineStateChanged(const magda::TimelineState& state,
                                             magda::ChangeFlags changes) {
    // Playhead changes
    if (magda::hasFlag(changes, magda::ChangeFlags::Playhead)) {
        double playPos = state.playhead.playbackPosition;

        // Auto-hide edit cursor when playback starts
        if (state.playhead.isPlaying && state.editCursorPosition >= 0.0) {
            if (auto* controller = magda::TimelineController::getCurrent()) {
                controller->dispatch(magda::SetEditCursorEvent{-1.0});
            }
        }

        // Offset playhead so it starts from the midiOffset position
        if (editingClipId_ != magda::INVALID_CLIP_ID) {
            const auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
            if (clip && clip->midiOffset > 0.0) {
                double secondsPerBeat = 60.0 / state.tempo.bpm;
                playPos += clip->midiOffset * secondsPerBeat;
            }
        }

        setGridPlayheadPosition(playPos);
        if (timeRuler_) {
            timeRuler_->setPlayheadPosition(playPos);
        }
    }

    // Edit cursor changes (SetEditCursorEvent returns Selection flag)
    if (magda::hasFlag(changes, magda::ChangeFlags::Selection)) {
        double editPos = state.editCursorPosition;
        bool visible = editPos >= 0.0;

        // Start/stop blink timer
        if (visible && !blinkTimer_.isTimerRunning()) {
            editCursorBlinkVisible_ = true;
            blinkTimer_.startTimerHz(2);  // ~500ms blink
        } else if (!visible && blinkTimer_.isTimerRunning()) {
            blinkTimer_.stopTimer();
            editCursorBlinkVisible_ = true;
        }

        setGridEditCursorPosition(editPos, visible && editCursorBlinkVisible_);
        if (timeRuler_) {
            timeRuler_->setEditCursorPosition(editPos, editCursorBlinkVisible_);
        }
    }

    // Tempo, timeline, or zoom changes — update ruler and grid
    if (magda::hasFlag(changes, magda::ChangeFlags::Tempo) ||
        magda::hasFlag(changes, magda::ChangeFlags::Timeline) ||
        magda::hasFlag(changes, magda::ChangeFlags::Zoom)) {
        updateTimeRuler();
        updateGridSize();
        repaint();
    }
}

// ============================================================================
// Grid resolution
// ============================================================================

void MidiEditorContent::updateGridResolution() {
    // Only auto-compute when the clip's autoGrid is enabled;
    // otherwise leave gridResolutionBeats_ at the manual value set by applyClipGridSettings.
    if (editingClipId_ != magda::INVALID_CLIP_ID) {
        const auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
        if (clip && !clip->gridAutoGrid) {
            return;  // Manual grid — don't overwrite
        }
    }

    constexpr int minPixelSpacing = 20;
    double frac = magda::GridConstants::findBeatSubdivision(horizontalZoom_, minPixelSpacing);
    double newResolution = (frac > 0.0) ? frac : 1.0;

    if (newResolution != gridResolutionBeats_) {
        gridResolutionBeats_ = newResolution;
        onGridResolutionChanged();

        // Notify BottomPanel to update its num/den display
        if (onAutoGridDisplayChanged) {
            int den = static_cast<int>(std::round(4.0 / gridResolutionBeats_));
            if (den < 1)
                den = 1;
            onAutoGridDisplayChanged(1, den);
        }
    }
}

double MidiEditorContent::snapBeatToGrid(double beat) const {
    if (!snapEnabled_ || gridResolutionBeats_ <= 0.0) {
        return beat;
    }
    return std::round(beat / gridResolutionBeats_) * gridResolutionBeats_;
}

// ============================================================================
// Per-clip grid settings
// ============================================================================

void MidiEditorContent::applyClipGridSettings() {
    if (editingClipId_ != magda::INVALID_CLIP_ID) {
        const auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
        if (clip) {
            snapEnabled_ = clip->gridSnapEnabled;

            if (clip->gridAutoGrid) {
                // Auto-compute from zoom
                constexpr int minPixelSpacing = 20;
                double frac =
                    magda::GridConstants::findBeatSubdivision(horizontalZoom_, minPixelSpacing);
                gridResolutionBeats_ = (frac > 0.0) ? frac : 1.0;
            } else {
                // Manual: compute from numerator/denominator
                gridResolutionBeats_ =
                    (4.0 * clip->gridNumerator) / static_cast<double>(clip->gridDenominator);
            }
            // Always push to grid component (snap or resolution may have changed)
            onGridResolutionChanged();
            return;
        }
    }

    // No clip — fall back to auto-compute from zoom
    constexpr int minPixelSpacing = 20;
    double frac = magda::GridConstants::findBeatSubdivision(horizontalZoom_, minPixelSpacing);
    gridResolutionBeats_ = (frac > 0.0) ? frac : 1.0;
    onGridResolutionChanged();
}

void MidiEditorContent::setGridSettingsFromUI(bool autoGrid, int numerator, int denominator) {
    if (editingClipId_ != magda::INVALID_CLIP_ID) {
        magda::ClipManager::getInstance().setClipGridSettings(editingClipId_, autoGrid, numerator,
                                                              denominator);
        // applyClipGridSettings() will be called from clipPropertyChanged callback
    }
}

void MidiEditorContent::setSnapEnabledFromUI(bool enabled) {
    if (editingClipId_ != magda::INVALID_CLIP_ID) {
        magda::ClipManager::getInstance().setClipSnapEnabled(editingClipId_, enabled);
        snapEnabled_ = enabled;
    }
}

}  // namespace magda::daw::ui
