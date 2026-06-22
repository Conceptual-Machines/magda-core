#include "MidiEditorContent.hpp"

#include <algorithm>
#include <cmath>
#include <set>

#include "audio/MidiBridge.hpp"
#include "core/ClipPropertyCommands.hpp"
#include "core/MidiNoteCommands.hpp"
#include "core/TrackManager.hpp"
#include "core/UndoManager.hpp"
#include "engine/AudioEngine.hpp"
#include "ui/components/pianoroll/MidiDrawerComponent.hpp"
#include "ui/components/pianoroll/VelocityLaneComponent.hpp"
#include "ui/components/timeline/TimeRuler.hpp"
#include "ui/state/TimelineController.hpp"
#include "ui/state/TimelineEvents.hpp"
#include "ui/state/TimelineState.hpp"
#include "ui/themes/CursorManager.hpp"
#include "ui/themes/DarkTheme.hpp"

namespace magda::daw::ui {

// Static members — persist across editor switches
bool MidiEditorContent::velocityDrawerOpen_ = false;
bool MidiEditorContent::velocityLaneVisible_ = false;
bool MidiEditorContent::foldEnabled_ = false;
std::vector<magda::TrackId> MidiEditorContent::overlayTrackIds_;

std::vector<int> MidiEditorContent::collectUsedPitches() const {
    std::set<int> used;
    if (editingClipId_ != magda::INVALID_CLIP_ID) {
        if (const auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_))
            for (const auto& note : clip->midiNotes)
                used.insert(note.noteNumber);
    }
    return std::vector<int>(used.begin(), used.end());
}

void MidiEditorContent::rebuildFoldMap() {
    foldMap_.rebuild(collectUsedPitches());
    onFoldMapChanged();
}

void MidiEditorContent::applyFold() {
    foldMap_.setEnabled(foldEnabled_);
    rebuildFoldMap();
    updateGridSize();
    recenterOnNotes();
}

namespace {
// Route clip timeline-seconds through the position-aware tempo facade when
// wired (message thread); fall back to the constant-tempo bpm before injection.
double facadeTimelineStart(const magda::ClipInfo& c, double bpm) {
    if (auto* tc = magda::TimelineController::getCurrent(); tc && tc->tempoMap())
        return c.getTimelineStart(*tc->tempoMap());
    return c.getTimelineStart(bpm);
}
double facadeTimelineLength(const magda::ClipInfo& c, double bpm) {
    if (auto* tc = magda::TimelineController::getCurrent(); tc && tc->tempoMap())
        return c.getTimelineLength(*tc->tempoMap());
    return c.getTimelineLength(bpm);
}

double effectiveLoopLengthSeconds(const magda::ClipInfo& clip, double bpm) {
    if (clip.loopLength > 0.0)
        return clip.loopLength;

    if (clip.loopLengthBeats > 0.0 && isValidBpm(bpm))
        return clip.loopLengthBeats * 60.0 / bpm;

    return facadeTimelineLength(clip, bpm);
}

double effectiveLoopStartSeconds(const magda::ClipInfo& clip, double bpm) {
    if (clip.loopStart > 0.0)
        return clip.loopStart;

    if (clip.loopStartBeats > 0.0 && isValidBpm(bpm))
        return clip.loopStartBeats * 60.0 / bpm;

    return 0.0;
}

bool usesRelativeLoopPhaseView(bool relativeMode, const magda::ClipInfo* clip, double bpm) {
    return relativeMode && clip && clip->loopEnabled &&
           effectiveLoopLengthSeconds(*clip, bpm) > 0.0;
}

// Relative loop mode displays phase within the clip loop. Seeking from that display
// phase preserves the current global loop cycle instead of jumping to the first cycle.
double relativeDisplaySecondsForGlobalPlayhead(double globalSeconds, const magda::ClipInfo* clip,
                                               double bpm, bool relativeMode) {
    if (!clip || !relativeMode)
        return globalSeconds;

    const double clipStart = facadeTimelineStart(*clip, bpm);
    const double clipLength = facadeTimelineLength(*clip, bpm);
    const double clipEnd = clipStart + clipLength;
    if (globalSeconds < clipStart || (clipLength > 0.0 && globalSeconds > clipEnd))
        return -1.0;

    if (!usesRelativeLoopPhaseView(relativeMode, clip, bpm))
        return globalSeconds - clipStart;

    const double loopStart = effectiveLoopStartSeconds(*clip, bpm);
    const double loopLength = effectiveLoopLengthSeconds(*clip, bpm);
    return loopStart + magda::wrapPhase(globalSeconds - clipStart - loopStart, loopLength);
}

double globalSecondsForRelativeDisplayClick(double displaySeconds, double currentGlobalSeconds,
                                            const magda::ClipInfo* clip, double bpm,
                                            bool relativeMode) {
    if (!clip || !relativeMode)
        return displaySeconds;

    const double clipStart = facadeTimelineStart(*clip, bpm);
    if (!usesRelativeLoopPhaseView(relativeMode, clip, bpm))
        return clipStart + displaySeconds;

    const double loopStart = effectiveLoopStartSeconds(*clip, bpm);
    const double loopLength = effectiveLoopLengthSeconds(*clip, bpm);
    const double phase = magda::wrapPhase(displaySeconds - loopStart, loopLength);
    const double currentElapsed = currentGlobalSeconds - clipStart;
    const double cycle =
        currentElapsed >= loopStart ? std::floor((currentElapsed - loopStart) / loopLength) : 0.0;

    double target = clipStart + loopStart + cycle * loopLength + phase;

    const double clipLength = facadeTimelineLength(*clip, bpm);
    if (clipLength <= 0.0)
        return juce::jmax(0.0, target);

    const double clipEnd = clipStart + clipLength;
    while (target < clipStart)
        target += loopLength;
    while (target > clipEnd)
        target -= loopLength;

    return juce::jlimit(clipStart, clipEnd, target);
}
}  // namespace

VerticalZoomStrip::VerticalZoomStrip(int minValue, int maxValue)
    : minValue_(minValue), maxValue_(maxValue) {
    setName("VerticalZoomStrip");
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
}

void VerticalZoomStrip::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds();
    g.fillAll(magda::DarkTheme::getColour(magda::DarkTheme::BACKGROUND_ALT));

    g.setColour(magda::DarkTheme::getColour(magda::DarkTheme::SEPARATOR));
    g.drawVerticalLine(bounds.getX(), 0.0f, static_cast<float>(bounds.getBottom()));
    g.drawVerticalLine(bounds.getRight() - 1, 0.0f, static_cast<float>(bounds.getBottom()));

    const int centreX = bounds.getCentreX();
    const int centreY = bounds.getCentreY();
    g.setColour(magda::DarkTheme::getColour(magda::DarkTheme::TEXT_DIM));
    for (int y = centreY - 18; y <= centreY + 18; y += 9)
        g.fillEllipse(static_cast<float>(centreX - 1), static_cast<float>(y - 1), 2.0f, 2.0f);
}

void VerticalZoomStrip::mouseDown(const juce::MouseEvent& event) {
    mouseDownX_ = event.x;
    mouseDownY_ = event.y;
    startValue_ = juce::jlimit(minValue_, maxValue_, getValue ? getValue() : minValue_);
    lastSentValue_ = startValue_;
    dragging_ = false;
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
}

void VerticalZoomStrip::mouseDrag(const juce::MouseEvent& event) {
    const int deltaX = event.x - mouseDownX_;
    const int deltaY = mouseDownY_ - event.y;
    const auto axis = std::abs(deltaX) > std::abs(deltaY) ? magda::GestureAxis::Horizontal
                                                          : magda::GestureAxis::Vertical;
    const int dragDelta = axis == magda::GestureAxis::Horizontal ? deltaX : deltaY;
    if (std::abs(dragDelta) > 3)
        dragging_ = true;

    if (!dragging_)
        return;

    const auto gesture = magda::GestureRouter::getInstance().resolveDrag(
        gestureContext_, magda::GestureArea::ZoomStrip, axis, event.mods,
        static_cast<float>(dragDelta), {mouseDownX_, mouseDownY_});
    if (gesture.type != magda::GestureActionType::ZoomVertical)
        return;

    const int rawValue = static_cast<int>(
        std::round(static_cast<double>(startValue_) * std::pow(2.0, gesture.magnitude)));
    const int newValue = juce::jlimit(minValue_, maxValue_, rawValue);
    if (newValue == lastSentValue_)
        return;

    lastSentValue_ = newValue;

    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);

    if (onZoomChanged)
        onZoomChanged(newValue, mouseDownY_);
}

void VerticalZoomStrip::mouseUp(const juce::MouseEvent& /*event*/) {
    dragging_ = false;
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
}

void VerticalZoomStrip::mouseMove(const juce::MouseEvent& /*event*/) {
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
}

void VerticalZoomStrip::mouseExit(const juce::MouseEvent& /*event*/) {
    setMouseCursor(juce::MouseCursor::NormalCursor);
}

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

    // TimeRuler upper click callback — set local edit cursor (independent from arrangement)
    timeRuler_->onPositionClicked = [this](double time, bool) { setLocalEditCursor(time); };

    // TimeRuler lower strip click callback — set the global arrangement playhead.
    timeRuler_->onPlayheadPositionClicked = [this](double time, bool bypassSnap) {
        auto* controller = magda::TimelineController::getCurrent();
        if (!controller)
            return;

        double tempo = controller->getState().tempo.bpm;
        if (!isValidBpm(tempo))
            tempo = DEFAULT_BPM;

        const auto& state = controller->getState();
        const auto* clip = editingClipId_ != magda::INVALID_CLIP_ID
                               ? magda::ClipManager::getInstance().getClip(editingClipId_)
                               : nullptr;
        const double absoluteSeconds = globalSecondsForRelativeDisplayClick(
            time, state.playhead.getCurrentPosition(), clip, tempo, relativeTimeMode_);

        double positionBeats = absoluteSeconds * tempo / 60.0;
        if (!bypassSnap)
            positionBeats = state.snapBeatsToGrid(positionBeats);
        controller->dispatch(magda::SetPlayheadPositionBeatsEvent{positionBeats});
    };

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

        double bpm = 120.0;
        if (auto* controller = magda::TimelineController::getCurrent())
            bpm = controller->getState().tempo.bpm;

        double newLoopStart =
            relativeTimeMode_ ? displayStart : (displayStart - facadeTimelineStart(*clip, bpm));
        double newLoopLength = displayEnd - displayStart;

        // Update TimeRuler's loop state so the background tint follows the drag
        timeRuler_->setLoopRegion(newLoopStart, newLoopLength, true);

        // Update grid loop region visually (lightweight — no note rebuild)
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

        double bpm = 120.0;
        if (auto* controller = magda::TimelineController::getCurrent())
            bpm = controller->getState().tempo.bpm;

        double newLoopStart =
            relativeTimeMode_ ? displayStart : (displayStart - facadeTimelineStart(*clip, bpm));
        double newLoopLength = displayEnd - displayStart;

        magda::UndoManager::getInstance().executeCommand(
            std::make_unique<magda::SetClipLoopRangeCommand>(editingClipId_, newLoopStart,
                                                             newLoopLength, bpm));
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

        magda::UndoManager::getInstance().executeCommand(
            std::make_unique<magda::SetClipOffsetCommand>(editingClipId_, phaseBeats));
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
        if (clip && clip->isMidi()) {
            editingClipId_ = selectedClip;
        }
    }

    // Initialize grid from clip settings (or auto-compute from zoom)
    applyClipGridSettings();
}

MidiEditorContent::~MidiEditorContent() {
    blinkTimer_.stopTimer();
    uninstallMidiNoteMonitor();  // safety net; subclasses uninstall in onDeactivated
    magda::ClipManager::getInstance().removeListener(this);

    if (auto* controller = magda::TimelineController::getCurrent()) {
        controller->removeListener(this);
    }
}

// ============================================================================
// Live MIDI note monitor (shared by PianoRoll keyboard + DrumGrid pad rows)
// ============================================================================

void MidiEditorContent::installMidiNoteMonitor() {
    auto* engine = magda::TrackManager::getInstance().getAudioEngine();
    auto* midiBridge = engine != nullptr ? engine->getMidiBridge() : nullptr;
    if (midiBridge == nullptr)
        return;

    if (midiNoteMonitorInstalled_ && monitoredMidiBridge_ == midiBridge)
        return;

    uninstallMidiNoteMonitor();

    monitoredMidiBridge_ = midiBridge;
    previousMidiNoteCallback_ = midiBridge->onNoteEvent;
    juce::Component::SafePointer<MidiEditorContent> safeThis(this);
    auto previousCallback = previousMidiNoteCallback_;

    midiBridge->onNoteEvent = [safeThis, previousCallback](magda::TrackId trackId,
                                                           const magda::MidiNoteEvent& event) {
        if (previousCallback)
            previousCallback(trackId, event);

        juce::MessageManager::callAsync([safeThis, trackId, event]() {
            if (auto* self = safeThis.getComponent())
                self->handleMidiNoteEvent(trackId, event);
        });
    };
    midiNoteMonitorInstalled_ = true;
}

void MidiEditorContent::uninstallMidiNoteMonitor() {
    if (midiNoteMonitorInstalled_ && monitoredMidiBridge_ != nullptr)
        monitoredMidiBridge_->onNoteEvent = previousMidiNoteCallback_;

    midiNoteMonitorInstalled_ = false;
    monitoredMidiBridge_ = nullptr;
    previousMidiNoteCallback_ = nullptr;
}

void MidiEditorContent::handleMidiNoteEvent(magda::TrackId trackId,
                                            const magda::MidiNoteEvent& event) {
    if (!midiNoteMonitorInstalled_ || editingClipId_ == magda::INVALID_CLIP_ID)
        return;

    const auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
    if (clip == nullptr || clip->trackId != trackId)
        return;

    const bool noteOn = event.isNoteOn && event.velocity > 0;

    // Only highlight notes the track is actually monitoring — with input
    // monitoring off the note never reaches the track, so highlighting it would
    // be misleading. Note-offs always fall through to clear any existing
    // highlight, so toggling monitor off mid-hold can't strand a pressed key.
    if (noteOn) {
        const auto* track = magda::TrackManager::getInstance().getTrack(trackId);
        if (track == nullptr || track->inputMonitor == magda::InputMonitorMode::Off)
            return;
    }

    highlightMonitoredNote(event.noteNumber, noteOn);

    // Bring the played note into view only when it falls off-screen, so live
    // input stays visible without yanking the view around on every note.
    if (noteOn)
        ensureMonitoredNoteVisible(event.noteNumber);
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
    timeRuler_->setBarOrigin(0.0);

    // Get timeline length
    double timelineLength = 300.0;
    if (auto* controller = magda::TimelineController::getCurrent()) {
        timelineLength = controller->getState().timelineLength;
    }
    timeRuler_->setTimelineLength(timelineLength);

    // Set zoom and grid resolution (pixels per beat)
    timeRuler_->setZoom(horizontalZoom_);
    timeRuler_->setGridResolution(gridResolutionBeats_);
    timeRuler_->setSnapEnabled(snapEnabled_);

    // Set clip info for boundary drawing.
    // Looped clips show the loop region starting from bar 1 — the editor
    // displays the loop content, not the timeline position.
    if (clip) {
        if (clip->loopEnabled || clip->view == magda::ClipView::Session) {
            timeRuler_->setTimeOffset(0.0);
            timeRuler_->setClipLength(facadeTimelineLength(*clip, tempo));
        } else {
            timeRuler_->setTimeOffset(facadeTimelineStart(*clip, tempo));
            timeRuler_->setClipLength(facadeTimelineLength(*clip, tempo));
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

    if (auto* controller = magda::TimelineController::getCurrent()) {
        const auto& state = controller->getState();
        double handlePosition = relativeDisplaySecondsForGlobalPlayhead(
            state.playhead.editPosition, clip, tempo, relativeTimeMode_);

        timeRuler_->setPlayheadHandlePosition(handlePosition);
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
        scrollToClipStartForTimeMode();
        repaint();
    }
}

void MidiEditorContent::scrollToClipStartForTimeMode() {
    if (!viewport_)
        return;

    int scrollX = 0;
    const auto* clip = editingClipId_ != magda::INVALID_CLIP_ID
                           ? magda::ClipManager::getInstance().getClip(editingClipId_)
                           : nullptr;

    if (!relativeTimeMode_ && clip && clip->view != magda::ClipView::Session)
        scrollX = static_cast<int>(std::round(clip->placement.startBeat * horizontalZoom_));

    viewport_->setViewPosition(scrollX, viewport_->getViewPositionY());
    lastScrolledPlacementStartBeat_ =
        clip ? clip->placement.startBeat : std::numeric_limits<double>::quiet_NaN();
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
        juce::MessageManager::callAsync([safeThis, clipId]() {
            if (auto* self = safeThis.getComponent()) {
                const auto* clip = magda::ClipManager::getInstance().getClip(clipId);
                const bool placementMoved =
                    clip && !self->relativeTimeMode_ &&
                    (std::isnan(self->lastScrolledPlacementStartBeat_) ||
                     std::abs(clip->placement.startBeat - self->lastScrolledPlacementStartBeat_) >
                         0.0001);

                self->applyClipGridSettings();
                self->updateGridSize();
                self->updateTimeRuler();
                if (placementMoved)
                    self->scrollToClipStartForTimeMode();
                self->repaint();
            }
        });
    }
}

// ============================================================================
// Multi-track overlay (ghost notes from other tracks, #1281)
// ============================================================================

void MidiEditorContent::showOverlayTracksMenu(juce::Component* anchor,
                                              std::function<void()> onChanged) {
    constexpr int CLEAR_ALL_ID = 9001;

    auto& trackManager = magda::TrackManager::getInstance();
    auto& clipManager = magda::ClipManager::getInstance();

    // The edited clip's track renders at full strength already
    magda::TrackId activeTrackId = magda::INVALID_TRACK_ID;
    if (const auto* clip = clipManager.getClip(editingClipId_))
        activeTrackId = clip->trackId;

    juce::PopupMenu menu;
    menu.addSectionHeader("Overlay Tracks");

    // One entry per other track that has MIDI clips, in its track colour
    std::vector<magda::TrackId> menuTracks;
    for (const auto& track : trackManager.getTracks()) {
        if (track.id == activeTrackId)
            continue;

        bool hasMidi = false;
        for (magda::ClipId cid : clipManager.getClipsOnTrack(track.id)) {
            const auto* clip = clipManager.getClip(cid);
            if (clip && clip->isMidi()) {
                hasMidi = true;
                break;
            }
        }
        if (!hasMidi)
            continue;

        juce::PopupMenu::Item item(track.name);
        item.itemID = static_cast<int>(menuTracks.size()) + 1;
        item.isTicked = std::find(overlayTrackIds_.begin(), overlayTrackIds_.end(), track.id) !=
                        overlayTrackIds_.end();
        item.colour = track.colour;
        menu.addItem(item);
        menuTracks.push_back(track.id);
    }

    if (menuTracks.empty()) {
        juce::PopupMenu::Item placeholder("No other MIDI tracks");
        placeholder.itemID = -2;
        placeholder.isEnabled = false;
        menu.addItem(placeholder);
    }

    menu.addSeparator();
    menu.addItem(CLEAR_ALL_ID, "Clear All", !overlayTrackIds_.empty());

    auto safeThis = juce::Component::SafePointer<MidiEditorContent>(this);
    auto safeAnchor = juce::Component::SafePointer<juce::Component>(anchor);

    auto options = juce::PopupMenu::Options();
    if (anchor)
        options = options.withTargetComponent(anchor);

    menu.showMenuAsync(
        options, [safeThis, safeAnchor, menuTracks, onChanged = std::move(onChanged)](int result) {
            if (!safeThis || result == 0 || result == -2)
                return;

            if (result == CLEAR_ALL_ID) {
                overlayTrackIds_.clear();
            } else if (result >= 1 && result <= static_cast<int>(menuTracks.size())) {
                const auto trackId = menuTracks[static_cast<size_t>(result - 1)];
                auto it = std::find(overlayTrackIds_.begin(), overlayTrackIds_.end(), trackId);
                if (it != overlayTrackIds_.end())
                    overlayTrackIds_.erase(it);
                else
                    overlayTrackIds_.push_back(trackId);
            } else {
                return;
            }

            safeThis->applyOverlayTracks();
            if (onChanged)
                onChanged();

            // Per-track toggles keep the menu session going (multi-select);
            // Clear All is a one-shot action
            if (result != CLEAR_ALL_ID)
                safeThis->showOverlayTracksMenu(safeAnchor.getComponent(), onChanged);
        });
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

        // Session mode: each clip owns its playhead via ClipInfo::sessionPlayheadPos
        const auto* editClip = (editingClipId_ != magda::INVALID_CLIP_ID)
                                   ? magda::ClipManager::getInstance().getClip(editingClipId_)
                                   : nullptr;
        if (editClip && editClip->sessionPlayheadPos >= 0.0) {
            double secondsPerBeat = 60.0 / state.tempo.bpm;
            playPos = editClip->startTime + editClip->sessionPlayheadPos;

            if (editClip->midiOffset > 0.0) {
                playPos += editClip->midiOffset * secondsPerBeat;
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

            const auto* clip = editingClipId_ != magda::INVALID_CLIP_ID
                                   ? magda::ClipManager::getInstance().getClip(editingClipId_)
                                   : nullptr;
            double bpm = state.tempo.bpm;
            if (!isValidBpm(bpm))
                bpm = DEFAULT_BPM;
            double handlePosition = relativeDisplaySecondsForGlobalPlayhead(
                state.playhead.editPosition, clip, bpm, relativeTimeMode_);

            timeRuler_->setPlayheadHandlePosition(handlePosition);
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
        if (timeRuler_)
            timeRuler_->setSnapEnabled(snapEnabled_);
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
        velocityLane_->setClipStartBeats(clip->placement.startBeat);
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
// MIDI Drawer (stacked lanes: velocity + CC + pitchbend)
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

    // Adding/removing a CC lane recomputes whether the drawer is shown (so a CC
    // lane can open the drawer without the velocity lane, and removing the last
    // lane closes it) and refreshes the sidebar toggle states.
    midiDrawer_->setVelocityLaneVisible(velocityLaneVisible_);
    midiDrawer_->onLanesChanged = [this]() {
        refreshLaneDrawer();
        updateLaneToggleStates();
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

void MidiEditorContent::refreshLaneDrawer() {
    if (midiDrawer_)
        midiDrawer_->setVelocityLaneVisible(velocityLaneVisible_);
    // The drawer area is shown when either the velocity lane is toggled on or
    // any CC lane exists, so velocity and CC are independent.
    velocityDrawerOpen_ = velocityLaneVisible_ || (midiDrawer_ && midiDrawer_->hasExtraLanes());
    updateVelocityLane();
    resized();
    repaint();
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
        midiDrawer_->setClipStartBeats(clip->placement.startBeat);
    } else {
        midiDrawer_->setClipStartBeats(0.0);
    }

    if (viewport_) {
        midiDrawer_->setScrollOffset(viewport_->getViewPositionX());
    }
}

}  // namespace magda::daw::ui
