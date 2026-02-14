#include "MidiEditorContent.hpp"

#include "ui/components/timeline/TimeRuler.hpp"
#include "ui/state/TimelineController.hpp"
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

    // Initialize grid resolution state (no dispatch — vtable not ready yet)
    if (auto* controller = magda::TimelineController::getCurrent()) {
        const auto& state = controller->getState();
        snapEnabled_ = state.display.snapEnabled;
        const auto& gq = state.display.gridQuantize;
        if (gq.autoGrid) {
            constexpr int minPixelSpacing = 20;
            double frac =
                magda::GridConstants::findBeatSubdivision(horizontalZoom_, minPixelSpacing);
            gridResolutionBeats_ = (frac > 0.0) ? frac : 1.0;
        } else {
            gridResolutionBeats_ = gq.toBeatFraction();
        }
    }
}

MidiEditorContent::~MidiEditorContent() {
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

    // Set zoom (pixels per beat)
    timeRuler_->setZoom(horizontalZoom_);

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
        setGridPlayheadPosition(state.playhead.playbackPosition);
        if (timeRuler_) {
            timeRuler_->setPlayheadPosition(state.playhead.playbackPosition);
        }
    }

    // Display changes — update grid resolution from BottomPanel controls
    if (magda::hasFlag(changes, magda::ChangeFlags::Display)) {
        updateGridResolution();
    }

    // Tempo, display, timeline, or zoom changes — update ruler and grid
    if (magda::hasFlag(changes, magda::ChangeFlags::Tempo) ||
        magda::hasFlag(changes, magda::ChangeFlags::Display) ||
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
    double newResolution = 0.25;  // Default 1/16
    bool newSnap = true;
    bool isAuto = true;

    if (auto* controller = magda::TimelineController::getCurrent()) {
        const auto& state = controller->getState();
        newSnap = state.display.snapEnabled;
        const auto& gq = state.display.gridQuantize;
        isAuto = gq.autoGrid;

        if (gq.autoGrid) {
            // Auto grid: pick subdivision based on zoom level (same logic as arrangement grid)
            constexpr int minPixelSpacing = 20;
            double frac =
                magda::GridConstants::findBeatSubdivision(horizontalZoom_, minPixelSpacing);
            if (frac > 0.0) {
                newResolution = frac;
            } else {
                // Very zoomed out — fall back to 1 beat
                newResolution = 1.0;
            }
        } else {
            // Manual grid: use the user's numerator/denominator setting
            newResolution = gq.toBeatFraction();
        }
    }

    bool changed = (newResolution != gridResolutionBeats_) || (newSnap != snapEnabled_);
    gridResolutionBeats_ = newResolution;
    snapEnabled_ = newSnap;

    if (changed) {
        onGridResolutionChanged();
    }
}

double MidiEditorContent::snapBeatToGrid(double beat) const {
    if (!snapEnabled_ || gridResolutionBeats_ <= 0.0) {
        return beat;
    }
    return std::round(beat / gridResolutionBeats_) * gridResolutionBeats_;
}

}  // namespace magda::daw::ui
