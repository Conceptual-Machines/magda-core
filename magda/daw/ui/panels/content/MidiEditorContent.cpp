#include "MidiEditorContent.hpp"

#include "core/MidiNoteCommands.hpp"
#include "core/UndoManager.hpp"
#include "ui/components/pianoroll/MidiDrawerComponent.hpp"
#include "ui/components/pianoroll/VelocityLaneComponent.hpp"
#include "ui/components/timeline/TimeRuler.hpp"
#include "ui/state/TimelineController.hpp"
#include "ui/state/TimelineEvents.hpp"
#include "ui/state/TimelineState.hpp"

namespace magda::daw::ui {

// Static member — persists drawer open/closed state across editor switches
bool MidiEditorContent::velocityDrawerOpen_ = false;

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

    // TimeRuler click callback — set local edit cursor (independent from arrangement)
    timeRuler_->onPositionClicked = [this](double time) { setLocalEditCursor(time); };

    // TimeRuler double-click on loop strip → zoom to loop region
    timeRuler_->onZoomToLoopRequested = [this](double startTime, double endTime) {
        zoomToTimeRange(startTime, endTime);
    };

    // TimeRuler loop region drag callback — visual preview only (no ClipManager commit)
    timeRuler_->onLoopRegionChanged = [this](double displayStart, double displayEnd) {
        if (editingClipId_ == magda::INVALID_CLIP_ID)
            return;
        const auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
        if (!clip || !clip->loopEnabled)
            return;

        double newLoopStart = relativeTimeMode_ ? displayStart : (displayStart - clip->startTime);
        double newLoopLength = displayEnd - displayStart;

        // Update TimeRuler's loop state so the background tint follows the drag
        timeRuler_->setLoopRegion(newLoopStart, newLoopLength, true);

        // Update grid loop region visually (lightweight — no note rebuild)
        auto* controller = magda::TimelineController::getCurrent();
        double bpm = controller ? controller->getState().tempo.bpm : 120.0;
        double beatsPerSecond = bpm / 60.0;

        previewLoopStartBeats_ = newLoopStart * beatsPerSecond;
        previewLoopLengthBeats_ = newLoopLength * beatsPerSecond;
        draggingLoopRegion_ = true;

        updateGridLoopRegion();
    };

    // TimeRuler loop drag ended — commit to ClipManager
    timeRuler_->onLoopDragEnded = [this](double displayStart, double displayEnd) {
        draggingLoopRegion_ = false;

        if (editingClipId_ == magda::INVALID_CLIP_ID)
            return;
        const auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
        if (!clip || !clip->loopEnabled)
            return;

        double newLoopStart = relativeTimeMode_ ? displayStart : (displayStart - clip->startTime);
        double newLoopLength = displayEnd - displayStart;

        auto* controller = magda::TimelineController::getCurrent();
        double bpm = controller ? controller->getState().tempo.bpm : 120.0;

        magda::ClipManager::getInstance().setLoopStartAndLength(editingClipId_, newLoopStart,
                                                                newLoopLength, bpm);
    };

    // TimeRuler phase marker drag callback — visual preview only
    timeRuler_->onPhaseMarkerChanged = [this](double phaseSeconds) {
        // Update ruler phase marker visually during drag
        timeRuler_->setLoopPhaseMarker(phaseSeconds, true);

        // Update grid phase marker preview
        auto* controller = magda::TimelineController::getCurrent();
        double bpm = controller ? controller->getState().tempo.bpm : 120.0;
        setGridPhasePreview(phaseSeconds * bpm / 60.0, true);
    };

    // Phase marker drag ended — commit to ClipManager
    timeRuler_->onPhaseDragEnded = [this](double phaseSeconds) {
        setGridPhasePreview(0.0, false);

        if (editingClipId_ == magda::INVALID_CLIP_ID)
            return;
        const auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
        if (!clip || !clip->loopEnabled)
            return;

        auto* controller = magda::TimelineController::getCurrent();
        double bpm = controller ? controller->getState().tempo.bpm : 120.0;
        double phaseBeats = phaseSeconds * bpm / 60.0;

        // Wrap within loop length
        double loopLengthBeats = clip->loopLength * bpm / 60.0;
        if (loopLengthBeats > 0.0) {
            phaseBeats = std::fmod(phaseBeats, loopLengthBeats);
            if (phaseBeats < 0.0)
                phaseBeats += loopLengthBeats;
        }

        magda::ClipManager::getInstance().setClipMidiOffset(editingClipId_, phaseBeats);
    };

    // Edit cursor blink timer (uses local cursor, not global)
    blinkTimer_.callback = [this]() {
        editCursorBlinkVisible_ = !editCursorBlinkVisible_;

        bool visible = localEditCursorPosition_ >= 0.0;
        setGridEditCursorPosition(localEditCursorPosition_, visible && editCursorBlinkVisible_);
        if (timeRuler_) {
            timeRuler_->setEditCursorPosition(localEditCursorPosition_, editCursorBlinkVisible_);
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
        int savedScrollY = viewport_->getViewPositionY();

        horizontalZoom_ = newPixelsPerBeat;
        setGridPixelsPerBeat(horizontalZoom_);
        updateGridResolution();
        updateGridSize();
        updateTimeRuler();
        updateMidiDrawer();
        updateVelocityLane();

        // Adjust scroll to keep anchor position under mouse
        int newAnchorX = static_cast<int>(anchorBeat * horizontalZoom_) + GRID_LEFT_PADDING;
        int newScrollX = newAnchorX - anchorScreenX;
        newScrollX = juce::jmax(0, newScrollX);
        viewport_->setViewPosition(newScrollX, savedScrollY);
    }
}

void MidiEditorContent::performWheelZoom(double zoomFactor, int mouseXInViewport) {
    int mouseXInContent = mouseXInViewport + viewport_->getViewPositionX();
    double anchorBeat = static_cast<double>(mouseXInContent - GRID_LEFT_PADDING) / horizontalZoom_;

    double newZoom = horizontalZoom_ * zoomFactor;
    newZoom = juce::jlimit(MIN_HORIZONTAL_ZOOM, MAX_HORIZONTAL_ZOOM, newZoom);

    if (newZoom != horizontalZoom_) {
        int savedScrollY = viewport_->getViewPositionY();

        horizontalZoom_ = newZoom;
        setGridPixelsPerBeat(horizontalZoom_);
        updateGridResolution();
        updateGridSize();
        updateTimeRuler();
        updateMidiDrawer();
        updateVelocityLane();

        // Adjust scroll position to keep anchor point under mouse
        int newAnchorX = static_cast<int>(anchorBeat * horizontalZoom_) + GRID_LEFT_PADDING;
        int newScrollX = newAnchorX - mouseXInViewport;
        newScrollX = juce::jmax(0, newScrollX);
        viewport_->setViewPosition(newScrollX, savedScrollY);
    }
}

// ============================================================================
// Zoom to time range
// ============================================================================

void MidiEditorContent::zoomToTimeRange(double startTime, double endTime) {
    if (endTime <= startTime || !viewport_)
        return;

    double tempo = 120.0;
    if (auto* controller = magda::TimelineController::getCurrent()) {
        tempo = controller->getState().tempo.bpm;
    }
    double secondsPerBeat = 60.0 / tempo;

    double startBeats = startTime / secondsPerBeat;
    double endBeats = endTime / secondsPerBeat;
    double durationBeats = endBeats - startBeats;
    double padding = durationBeats * 0.05;

    int viewWidth = viewport_->getWidth();
    double newZoom = static_cast<double>(viewWidth) / (durationBeats + padding * 2.0);
    newZoom = juce::jlimit(MIN_HORIZONTAL_ZOOM, MAX_HORIZONTAL_ZOOM, newZoom);

    horizontalZoom_ = newZoom;
    setGridPixelsPerBeat(horizontalZoom_);
    updateGridResolution();
    updateGridSize();
    updateTimeRuler();
    updateMidiDrawer();
    updateVelocityLane();

    int scrollX = static_cast<int>((startBeats - padding) * horizontalZoom_) + GRID_LEFT_PADDING;
    scrollX = juce::jmax(0, scrollX);
    viewport_->setViewPosition(scrollX, viewport_->getViewPositionY());
}

// ============================================================================
// TimeRuler
// ============================================================================

void MidiEditorContent::setLocalEditCursor(double positionSeconds) {
    localEditCursorPosition_ = positionSeconds;
    editCursorBlinkVisible_ = true;

    if (!blinkTimer_.isTimerRunning()) {
        blinkTimer_.startTimerHz(2);
    }

    setGridEditCursorPosition(positionSeconds, true);
    if (timeRuler_) {
        timeRuler_->setEditCursorPosition(positionSeconds, true);
    }
}

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

    // Set clip info for boundary drawing.
    // Looped clips show the loop region starting from bar 1 — the editor
    // displays the loop content, not the timeline position.
    if (clip) {
        if (clip->loopEnabled || clip->view == magda::ClipView::Session) {
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

    // Set loop region markers and phase marker
    if (clip) {
        timeRuler_->setLoopRegion(clip->loopStart, clip->loopLength, clip->loopEnabled);
        // Show yellow phase marker when looped
        if (clip->loopEnabled) {
            double phaseSeconds = clip->midiOffset * 60.0 / tempo;
            timeRuler_->setLoopPhaseMarker(phaseSeconds, clip->midiOffset > 0.0);
        } else {
            timeRuler_->setLoopPhaseMarker(0.0, false);
        }
    } else {
        timeRuler_->setLoopRegion(0.0, 0.0, false);
        timeRuler_->setLoopPhaseMarker(0.0, false);
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

        // Auto-hide local edit cursor when playback starts
        if (state.playhead.isPlaying && localEditCursorPosition_ >= 0.0) {
            localEditCursorPosition_ = -1.0;
            blinkTimer_.stopTimer();
            editCursorBlinkVisible_ = true;
            setGridEditCursorPosition(-1.0, false);
            if (timeRuler_) {
                timeRuler_->setEditCursorPosition(-1.0, false);
            }
        }

        // Session mode: use the loop-wrapped session playhead position
        // instead of the arrangement transport position, matching the
        // approach used by WaveformEditorContent.
        double sessionPos = state.playhead.sessionPlaybackPosition;
        if (sessionPos >= 0.0 && state.playhead.sessionPlaybackClipId == editingClipId_ &&
            editingClipId_ != magda::INVALID_CLIP_ID) {
            const auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
            if (clip) {
                // Convert session elapsed seconds to absolute seconds so the
                // grid's beat conversion (playheadBeats = pos / secondsPerBeat)
                // produces a beat offset relative to clip start.
                double secondsPerBeat = 60.0 / state.tempo.bpm;
                playPos = clip->startTime + sessionPos;

                if (clip->midiOffset > 0.0) {
                    playPos += clip->midiOffset * secondsPerBeat;
                }
            }
        } else {
            // Arrangement mode: offset playhead by midiOffset (beats → seconds)
            if (editingClipId_ != magda::INVALID_CLIP_ID) {
                const auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
                if (clip && clip->midiOffset > 0.0) {
                    double secondsPerBeat = 60.0 / state.tempo.bpm;
                    playPos += clip->midiOffset * secondsPerBeat;
                }
            }
        }

        // Only show playhead during playback
        double displayPos = state.playhead.isPlaying ? playPos : -1.0;
        setGridPlayheadPosition(displayPos);
        if (timeRuler_) {
            timeRuler_->setPlayheadPosition(displayPos);
        }
    }

    // Edit cursor: MIDI editor uses its own local cursor, but clears it
    // when the global cursor is explicitly hidden (e.g. Escape key).
    if (magda::hasFlag(changes, magda::ChangeFlags::Selection)) {
        if (state.editCursorPosition < 0.0 && localEditCursorPosition_ >= 0.0) {
            localEditCursorPosition_ = -1.0;
            blinkTimer_.stopTimer();
            editCursorBlinkVisible_ = true;
            setGridEditCursorPosition(-1.0, false);
            if (timeRuler_) {
                timeRuler_->setEditCursorPosition(-1.0, false);
            }
        }
    }

    // Tempo or timeline length changes — update ruler and grid
    // Note: do NOT respond to arrangement Zoom changes here;
    // the MIDI editor has its own independent zoom.
    if (magda::hasFlag(changes, magda::ChangeFlags::Tempo) ||
        magda::hasFlag(changes, magda::ChangeFlags::Timeline)) {
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

// ============================================================================
// Velocity lane
// ============================================================================

void MidiEditorContent::setupVelocityLane() {
    velocityLane_ = std::make_unique<magda::VelocityLaneComponent>();
    velocityLane_->setLeftPadding(GRID_LEFT_PADDING);
    velocityLane_->onVelocityChanged = [this](magda::ClipId clipId, size_t noteIndex,
                                              int newVelocity) {
        auto cmd =
            std::make_unique<magda::SetMidiNoteVelocityCommand>(clipId, noteIndex, newVelocity);
        magda::UndoManager::getInstance().executeCommand(std::move(cmd));
        onVelocityEdited();
    };
    velocityLane_->onMultiVelocityChanged = [this](magda::ClipId clipId,
                                                   std::vector<std::pair<size_t, int>> velocities) {
        auto cmd = std::make_unique<magda::SetMultipleNoteVelocitiesCommand>(clipId,
                                                                             std::move(velocities));
        magda::UndoManager::getInstance().executeCommand(std::move(cmd));
        onVelocityEdited();
    };
    addChildComponent(velocityLane_.get());
}

void MidiEditorContent::updateVelocityLane() {
    if (!velocityLane_)
        return;

    velocityLane_->setClip(editingClipId_);
    velocityLane_->setPixelsPerBeat(horizontalZoom_);
    velocityLane_->setRelativeMode(relativeTimeMode_);

    const auto* clip = editingClipId_ != magda::INVALID_CLIP_ID
                           ? magda::ClipManager::getInstance().getClip(editingClipId_)
                           : nullptr;

    if (clip) {
        double tempo = 120.0;
        if (auto* controller = magda::TimelineController::getCurrent()) {
            tempo = controller->getState().tempo.bpm;
        }
        double secondsPerBeat = 60.0 / tempo;
        double clipStartBeats = clip->startTime / secondsPerBeat;
        velocityLane_->setClipStartBeats(clipStartBeats);
    } else {
        velocityLane_->setClipStartBeats(0.0);
    }

    if (viewport_) {
        velocityLane_->setScrollOffset(viewport_->getViewPositionX());
    }
}

void MidiEditorContent::onVelocityEdited() {
    if (velocityLane_) {
        velocityLane_->refreshNotes();
    }
}

void MidiEditorContent::setVelocityLaneSelectedNotes(const std::vector<size_t>& indices) {
    if (velocityLane_) {
        velocityLane_->setSelectedNoteIndices(indices);
    }
    if (midiDrawer_ && midiDrawer_->getVelocityLane()) {
        midiDrawer_->getVelocityLane()->setSelectedNoteIndices(indices);
    }
}

// ============================================================================
// MIDI Drawer (tabbed: velocity + CC + pitchbend)
// ============================================================================

void MidiEditorContent::setupMidiDrawer() {
    midiDrawer_ = std::make_unique<magda::MidiDrawerComponent>();
    midiDrawer_->setLeftPadding(GRID_LEFT_PADDING);

    // Wire velocity callbacks through the drawer's velocity lane
    auto* velLane = midiDrawer_->getVelocityLane();
    if (velLane) {
        velLane->onVelocityChanged = [this](magda::ClipId clipId, size_t noteIndex,
                                            int newVelocity) {
            auto cmd =
                std::make_unique<magda::SetMidiNoteVelocityCommand>(clipId, noteIndex, newVelocity);
            magda::UndoManager::getInstance().executeCommand(std::move(cmd));
            onVelocityEdited();
        };
        velLane->onMultiVelocityChanged = [this](magda::ClipId clipId,
                                                 std::vector<std::pair<size_t, int>> velocities) {
            auto cmd = std::make_unique<magda::SetMultipleNoteVelocitiesCommand>(
                clipId, std::move(velocities));
            magda::UndoManager::getInstance().executeCommand(std::move(cmd));
            onVelocityEdited();
        };
    }

    midiDrawer_->onResizeDrag = [this](int newHeight) {
        int clamped = juce::jlimit(MIN_DRAWER_HEIGHT, MAX_DRAWER_HEIGHT, newHeight);
        if (clamped != drawerHeight_) {
            drawerHeight_ = clamped;
            resized();
        }
    };

    addChildComponent(midiDrawer_.get());
}

void MidiEditorContent::setVelocityDrawerVisible(bool visible) {
    if (velocityDrawerOpen_ != visible) {
        velocityDrawerOpen_ = visible;
        updateVelocityLane();
        resized();
        repaint();
    }
}

void MidiEditorContent::updateMidiDrawer() {
    if (!midiDrawer_)
        return;

    midiDrawer_->setClip(editingClipId_);
    midiDrawer_->setPixelsPerBeat(horizontalZoom_);
    midiDrawer_->setRelativeMode(relativeTimeMode_);

    const auto* clip = editingClipId_ != magda::INVALID_CLIP_ID
                           ? magda::ClipManager::getInstance().getClip(editingClipId_)
                           : nullptr;

    if (clip) {
        double tempo = 120.0;
        if (auto* controller = magda::TimelineController::getCurrent()) {
            tempo = controller->getState().tempo.bpm;
        }
        double secondsPerBeat = 60.0 / tempo;
        double clipStartBeats = clip->startTime / secondsPerBeat;
        midiDrawer_->setClipStartBeats(clipStartBeats);
    } else {
        midiDrawer_->setClipStartBeats(0.0);
    }

    if (viewport_) {
        midiDrawer_->setScrollOffset(viewport_->getViewPositionX());
    }
}

}  // namespace magda::daw::ui
