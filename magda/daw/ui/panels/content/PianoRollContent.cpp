#include "PianoRollContent.hpp"

#include <cmath>
#include <limits>
#include <set>

#include "../../core/SelectionManager.hpp"
#include "../../state/TimelineController.hpp"
#include "../../state/TimelineEvents.hpp"
#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "BinaryData.h"
#include "audio/MidiBridge.hpp"
#include "audio/plugins/MidiChordEnginePlugin.hpp"
#include "core/ChordAnnotationCommands.hpp"
#include "core/ChordProgressionContext.hpp"
#include "core/ChordProgressionConverter.hpp"
#include "core/GestureRouter.hpp"
#include "core/MidiNoteCommands.hpp"
#include "core/SelectionManager.hpp"
#include "core/TempoUtils.hpp"
#include "core/TrackManager.hpp"
#include "core/UndoManager.hpp"
#include "engine/AudioEngine.hpp"
#include "music/ChordEngine.hpp"
#include "music/NotationSettings.hpp"
#include "ui/components/common/SvgButton.hpp"
#include "ui/components/common/TimeBendPopup.hpp"
#include "ui/components/pianoroll/CCLaneComponent.hpp"
#include "ui/components/pianoroll/MidiDrawerComponent.hpp"
#include "ui/components/pianoroll/MidiTakeLanesComponent.hpp"
#include "ui/components/pianoroll/OctaveLabelStrip.hpp"
#include "ui/components/pianoroll/PianoRollGridComponent.hpp"
#include "ui/components/pianoroll/PianoRollKeyboard.hpp"
#include "ui/components/pianoroll/VelocityLaneComponent.hpp"
#include "ui/components/timeline/TimeRuler.hpp"

namespace magda::daw::ui {

bool PianoRollContent::showProgressionOverlay_ = false;

PianoRollContent::PianoRollContent() {
    setName("PianoRoll");
    if (timeRuler_)
        timeRuler_->setGestureContext(magda::GestureContext::PianoRoll);

    // Repaint the chord lane when the C / Do notation changes anywhere.
    magda::music::NotationSettings::getInstance().addChangeListener(this);

    // Create fold toggle button (collapse the vertical axis to used pitches)
    foldToggle_ = std::make_unique<magda::SvgButton>("FoldToggle", BinaryData::iconfoldboldm_svg,
                                                     BinaryData::iconfoldboldm_svgSize);
    foldToggle_->setTooltip("Fold to used pitches");
    foldToggle_->setOriginalColor(juce::Colour(0xFFB3B3B3));
    foldToggle_->setActive(foldEnabled_);
    foldToggle_->onClick = [this]() {
        foldEnabled_ = !foldEnabled_;
        foldToggle_->setActive(foldEnabled_);
        applyFold();
    };
    addAndMakeVisible(foldToggle_.get());

    // Create take-lanes toggle button (show/hide the comp take-lanes strip).
    // Only relevant for a MIDI clip with takes; shown contextually.
    takeLanesToggle_ = std::make_unique<magda::SvgButton>("TakeLanesToggle", BinaryData::lanes_svg,
                                                          BinaryData::lanes_svgSize);
    takeLanesToggle_->setTooltip("Show take lanes");
    takeLanesToggle_->setOriginalColor(juce::Colour(0xFFB3B3B3));
    takeLanesToggle_->onClick = [this]() {
        auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
        if (clip == nullptr)
            return;
        clip->takesExpanded = !clip->takesExpanded;
        magda::ClipManager::getInstance().forceNotifyClipPropertyChanged(editingClipId_);
    };
    addChildComponent(takeLanesToggle_.get());

    // Create chord toggle button
    chordToggle_ = std::make_unique<magda::SvgButton>("ChordToggle", BinaryData::iconchordboldm_svg,
                                                      BinaryData::iconchordboldm_svgSize);
    chordToggle_->setTooltip("Toggle chord row");
    chordToggle_->setOriginalColor(juce::Colour(0xFFB3B3B3));
    chordToggle_->setActive(showChordRow_);
    chordToggle_->onClick = [this]() {
        setChordRowVisible(!showChordRow_);
        chordToggle_->setActive(showChordRow_);
    };
    addAndMakeVisible(chordToggle_.get());

    // Create chord detect button (appears in chord row's keyboard column)
    chordDetectBtn_ = std::make_unique<magda::SvgButton>("ChordDetect", BinaryData::refresh_svg,
                                                         BinaryData::refresh_svgSize);
    chordDetectBtn_->setTooltip("Recalculate chords from notes");
    chordDetectBtn_->setOriginalColor(juce::Colour(0xFFE3E3E3));
    chordDetectBtn_->setNormalColor(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    chordDetectBtn_->onClick = [this]() { detectChordsFromNotes(); };
    chordDetectBtn_->setVisible(showChordRow_);
    addAndMakeVisible(chordDetectBtn_.get());

    // Progression overlay toggle: ghost the chord-track progression behind this
    // track's chord lane (#1504). Only meaningful on non-chord tracks.
    progressionOverlayToggle_ = std::make_unique<magda::SvgButton>(
        "ProgressionOverlay", BinaryData::iconchordtrackboldm_svg,
        BinaryData::iconchordtrackboldm_svgSize);
    progressionOverlayToggle_->setTooltip("Compare against the chord-track progression");
    progressionOverlayToggle_->setOriginalColor(juce::Colour(0xFFB3B3B3));
    progressionOverlayToggle_->setActive(showProgressionOverlay_);
    progressionOverlayToggle_->onClick = [this]() {
        showProgressionOverlay_ = !showProgressionOverlay_;
        progressionOverlayToggle_->setActive(showProgressionOverlay_);
        repaint();
    };
    progressionOverlayToggle_->setVisible(false);
    addAndMakeVisible(progressionOverlayToggle_.get());

    // Chord-focus mode shows this in place of the rescan button: a toggle that
    // shows/hides the note grid below the chord lane. A light chevron keeps the
    // gutter unobtrusive; it accents when the grid is shown.
    gridToggleBtn_ = std::make_unique<magda::SvgButton>("GridToggle", BinaryData::chevron_down_svg,
                                                        BinaryData::chevron_down_svgSize);
    gridToggleBtn_->setTooltip("Show / hide the piano roll");
    gridToggleBtn_->setOriginalColor(juce::Colour(0xFFE3E3E3));
    gridToggleBtn_->setNormalColor(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    gridToggleBtn_->onClick = [this]() { onGridToggleClicked(); };
    addChildComponent(gridToggleBtn_.get());

    // Create velocity toggle button (opens the lanes drawer)
    velocityToggle_ = std::make_unique<magda::SvgButton>(
        "VelocityToggle", BinaryData::iconvelocityboldm_svg, BinaryData::iconvelocityboldm_svgSize);
    velocityToggle_->setTooltip("Toggle velocity lane");
    velocityToggle_->setOriginalColor(juce::Colour(0xFFB3B3B3));
    velocityToggle_->setActive(velocityLaneVisible_);
    velocityToggle_->onClick = [this]() {
        velocityLaneVisible_ = !velocityLaneVisible_;
        refreshLaneDrawer();
        updateLaneToggleStates();
    };
    addAndMakeVisible(velocityToggle_.get());

    // Create pitch glide toggle button (MPE pitch expression overlay)
    pitchGlideToggle_ = std::make_unique<magda::SvgButton>(
        "PitchGlideToggle", BinaryData::iconmpeboldm_svg, BinaryData::iconmpeboldm_svgSize);
    pitchGlideToggle_->setTooltip("Toggle pitch glide editing (MPE)");
    pitchGlideToggle_->setOriginalColor(juce::Colour(0xFFB3B3B3));
    pitchGlideToggle_->setActive(false);
    pitchGlideToggle_->onClick = [this]() {
        const bool enabled = !gridComponent_->isPitchExpressionMode();
        gridComponent_->setPitchExpressionMode(enabled);
        pitchGlideToggle_->setActive(enabled);
    };
    addAndMakeVisible(pitchGlideToggle_.get());

    // Create CC lanes button (opens the drawer and the add-lane menu)
    ccLanesBtn_ = std::make_unique<magda::SvgButton>("CCLanes", BinaryData::iconccboldm_svg,
                                                     BinaryData::iconccboldm_svgSize);
    ccLanesBtn_->setTooltip("Add CC / pitchbend lane");
    ccLanesBtn_->setOriginalColor(juce::Colour(0xFFB3B3B3));
    ccLanesBtn_->onClick = [this]() {
        // Just open the add-lane menu; adding a CC lane opens the drawer on its
        // own (without forcing the velocity lane) via onLanesChanged.
        if (midiDrawer_)
            midiDrawer_->showAddLaneMenu();
    };
    addAndMakeVisible(ccLanesBtn_.get());

    verticalZoomStrip_ = std::make_unique<VerticalZoomStrip>(MIN_NOTE_HEIGHT, MAX_NOTE_HEIGHT);
    verticalZoomStrip_->setGestureContext(magda::GestureContext::PianoRoll);
    verticalZoomStrip_->getValue = [this]() { return noteHeight_; };
    verticalZoomStrip_->onZoomChanged = [this](int newHeight, int anchorScreenY) {
        const int anchorContentY = anchorScreenY + viewport_->getViewPositionY();
        const int anchorNote =
            gridComponent_
                ? gridComponent_->yToNoteNumber(anchorContentY)
                : juce::jlimit(MIN_NOTE, MAX_NOTE, MAX_NOTE - (anchorContentY / noteHeight_));
        setNoteHeightAnchored(newHeight, anchorNote, anchorScreenY, true);
    };
    addAndMakeVisible(verticalZoomStrip_.get());

    // Octave label strip — fixed narrow column between the zoom strip and
    // the keyboard, always shows C-x labels regardless of zoom level.
    octaveLabelStrip_ = std::make_unique<magda::OctaveLabelStrip>();
    octaveLabelStrip_->setNoteRange(MIN_NOTE, MAX_NOTE);
    octaveLabelStrip_->setNoteHeight(noteHeight_);
    addAndMakeVisible(octaveLabelStrip_.get());

    // Create keyboard component
    keyboard_ = std::make_unique<magda::PianoRollKeyboard>();
    keyboard_->setNoteHeight(noteHeight_);
    keyboard_->setNoteRange(MIN_NOTE, MAX_NOTE);

    // Set up vertical zoom callback from keyboard (drag up/down to zoom)
    keyboard_->onZoomChanged = [this](int newHeight, int anchorNote, int anchorScreenY) {
        setNoteHeightAnchored(newHeight, anchorNote, anchorScreenY, true);
    };

    // Set up vertical scroll callback from keyboard (drag left/right to scroll)
    keyboard_->onScrollRequested = [this](int deltaY) {
        int newScrollY = viewport_->getViewPositionY() + deltaY;
        newScrollY = juce::jmax(0, newScrollY);
        viewport_->setViewPosition(viewport_->getViewPositionX(), newScrollY);
    };

    // Set up note preview callback for keyboard click-to-play
    keyboard_->onNotePreview = [this](int noteNumber, int velocity, bool isNoteOn) {
        DBG("PianoRollContent: Note preview callback - Note="
            << noteNumber << ", Velocity=" << velocity << ", On=" << (isNoteOn ? "YES" : "NO"));

        // Get track ID from currently edited clip
        if (editingClipId_ != magda::INVALID_CLIP_ID) {
            const auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
            if (clip && clip->trackId != magda::INVALID_TRACK_ID) {
                DBG("PianoRollContent: Calling TrackManager::previewNote for track "
                    << clip->trackId);
                // Preview note through track's instruments
                magda::TrackManager::getInstance().previewNote(clip->trackId, noteNumber, velocity,
                                                               isNoteOn);
            } else {
                DBG("PianoRollContent: No valid clip or track ID");
            }
        } else {
            DBG("PianoRollContent: No clip being edited");
        }
    };

    addAndMakeVisible(keyboard_.get());

    // Add PianoRoll-specific components to viewport repaint list
    viewport_->componentsToRepaint.push_back(keyboard_.get());
    viewport_->componentsToRepaint.push_back(this);  // For chord row repaint

    // Create the grid component
    gridComponent_ = std::make_unique<magda::PianoRollGridComponent>();
    gridComponent_->setPixelsPerBeat(horizontalZoom_);
    gridComponent_->setNoteHeight(noteHeight_);
    gridComponent_->setLeftPadding(GRID_LEFT_PADDING);
    gridComponent_->setGridResolutionBeats(gridResolutionBeats_);
    gridComponent_->setSnapEnabled(snapEnabled_);
    gridComponent_->onSelectedPitchRowsChanged = [this](const std::set<int>& notes) {
        if (keyboard_)
            keyboard_->setHighlightedNotes(notes);
    };
    gridComponent_->onVerticalZoomRequested = [this](int gridY,
                                                     const juce::MouseWheelDetails& wheel) {
        const int anchorScreenY = gridY - viewport_->getViewPositionY();
        const int anchorNote =
            gridComponent_
                ? gridComponent_->yToNoteNumber(gridY)
                : juce::jlimit(MIN_NOTE, MAX_NOTE, MAX_NOTE - (gridY / juce::jmax(1, noteHeight_)));
        const int heightDelta = wheel.deltaY > 0 ? 2 : -2;
        setNoteHeightAnchored(noteHeight_ + heightDelta, anchorNote, anchorScreenY, true);
    };
    if (auto* controller = magda::TimelineController::getCurrent()) {
        gridComponent_->setTimeSignatureNumerator(
            controller->getState().tempo.timeSignatureNumerator);
    }
    viewport_->setViewedComponent(gridComponent_.get(), false);

    // Share one folded-axis map across grid, keyboard, and octave strip so their
    // vertical axes stay aligned. foldMap_ outlives all three (we own them).
    foldMap_.setEnabled(foldEnabled_);
    gridComponent_->setFoldMap(&foldMap_);
    keyboard_->setFoldMap(&foldMap_);
    octaveLabelStrip_->setFoldMap(&foldMap_);

    setupGridCallbacks();

    // Folded take-lanes strip below the grid (MIDI comping, #1466).
    takeLanes_ = std::make_unique<MidiTakeLanesComponent>();
    takeLanes_->setLeftPadding(GRID_LEFT_PADDING);
    takeLanes_->onTakeSelected = [this](int takeIndex) {
        magda::ClipManager::getInstance().setMidiClipCurrentTake(editingClipId_, takeIndex);
    };
    takeLanes_->onCompSectionSet = [this](double startBeat, double endBeat, int takeIndex) {
        magda::ClipManager::getInstance().setMidiCompSection(editingClipId_, startBeat, endBeat,
                                                             takeIndex);
    };
    takeLanes_->onCompClear = [this]() {
        magda::ClipManager::getInstance().clearMidiComp(editingClipId_);
    };
    takeLanes_->onDeleteTake = [this](int takeIndex) {
        magda::ClipManager::getInstance().deleteClipTake(editingClipId_, takeIndex);
    };
    takeLanes_->onTakeHovered = [this](int takeIndex) {
        const auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
        if (!gridComponent_ || clip == nullptr || !clip->isMidi()) {
            if (gridComponent_)
                gridComponent_->clearOverlayNotes();
            return;
        }
        const auto& takes = clip->midi().takes;
        if (takeIndex >= 0 && takeIndex < static_cast<int>(takes.size()))
            gridComponent_->setOverlayNotes(takes[static_cast<size_t>(takeIndex)].notes,
                                            clip->colour);
        else
            gridComponent_->clearOverlayNotes();
    };
    addChildComponent(takeLanes_.get());
    viewport_->componentsToRepaint.push_back(takeLanes_.get());

    // Apply any overlay tracks chosen in another editor session
    applyOverlayTracks();

    // Setup MIDI drawer (stacked lanes: velocity + CC + pitchbend). The base
    // wires onLanesChanged -> refreshLaneDrawer + updateLaneToggleStates().
    setupMidiDrawer();

    // Register as SelectionManager listener (PianoRoll-specific)
    magda::SelectionManager::getInstance().addListener(this);

    // If base found a selected clip, set it up on our grid
    if (editingClipId_ != magda::INVALID_CLIP_ID) {
        loadNoteHeightFromClip(editingClipId_);
        gridComponent_->setClip(editingClipId_);
        updateTimeRuler();
    }
}

void PianoRollContent::applyOverlayTracks() {
    if (gridComponent_)
        gridComponent_->setOverlayTracks(overlayTrackIds_);
}

bool PianoRollContent::takeLanesVisible() const {
    const auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
    return clip != nullptr && clip->isMidi() && clip->midi().takes.size() >= 2 &&
           clip->takesExpanded;
}

void PianoRollContent::refreshTakeLanes() {
    if (!takeLanes_)
        return;
    takeLanes_->setClip(editingClipId_);
    takeLanes_->setPixelsPerBeat(horizontalZoom_);
    takeLanes_->setRelativeMode(relativeTimeMode_);
    takeLanes_->setGridResolutionBeats(gridResolutionBeats_);
    takeLanes_->setSnapEnabled(snapEnabled_);
    takeLanes_->setScrollOffset(viewport_ ? viewport_->getViewPositionX() : 0);
}

std::vector<int> PianoRollContent::collectUsedPitches() const {
    // Union of used pitches across the editing clip, or all selected clips in
    // multi-clip mode. Folding only reflects the editable clip(s)' own notes
    // (not ghost-overlay tracks).
    std::set<int> usedPitches;
    auto& clipManager = magda::ClipManager::getInstance();
    const auto& selectedClipIds =
        gridComponent_ ? gridComponent_->getSelectedClipIds() : std::vector<magda::ClipId>{};
    auto collect = [&](magda::ClipId id) {
        if (const auto* clip = clipManager.getClip(id))
            for (const auto& note : clip->midiNotes)
                usedPitches.insert(note.noteNumber);
    };
    if (selectedClipIds.size() > 1) {
        for (magda::ClipId id : selectedClipIds)
            collect(id);
    } else if (editingClipId_ != magda::INVALID_CLIP_ID) {
        collect(editingClipId_);
    }
    return std::vector<int>(usedPitches.begin(), usedPitches.end());
}

void PianoRollContent::onFoldMapChanged() {
    // Keyboard / octave strip / grid follow the new row set.
    if (gridComponent_)
        gridComponent_->repaint();
    if (keyboard_)
        keyboard_->repaint();
    if (octaveLabelStrip_)
        octaveLabelStrip_->repaint();
}

void PianoRollContent::recenterOnNotes() {
    // After a fold toggle the content height jumps and the previous scroll maps
    // to a wildly different pitch (folded rows expand back across the full
    // 0..127 axis, so a folded view sitting on octave 4 lands on octave 7/8 when
    // unfolded). Recenter on the notes whichever way we toggled.
    centerOnNotes();
}

void PianoRollContent::updateLaneToggleStates() {
    if (velocityToggle_)
        velocityToggle_->setActive(velocityLaneVisible_);
    if (ccLanesBtn_ && midiDrawer_)
        ccLanesBtn_->setActive(midiDrawer_->hasExtraLanes());
}

PianoRollContent::~PianoRollContent() {
    uninstallMidiNoteMonitor();
    magda::SelectionManager::getInstance().removeListener(this);
    magda::music::NotationSettings::getInstance().removeChangeListener(this);
}

void PianoRollContent::setNoteHeight(int height, bool persist) {
    // Don't allow zooming below the level where the octave label strip's
    // labels would no longer fit. Each octave block must be at least
    // LABEL_HEIGHT pixels tall.
    constexpr int kLabelFloor =
        (magda::OctaveLabelStrip::LABEL_HEIGHT + 11) / 12;  // ceil(14/12) = 2
    const int effectiveMin = juce::jmax(MIN_NOTE_HEIGHT, kLabelFloor);
    const int clampedHeight = juce::jlimit(effectiveMin, MAX_NOTE_HEIGHT, height);
    if (clampedHeight == noteHeight_) {
        return;
    }

    noteHeight_ = clampedHeight;
    if (gridComponent_)
        gridComponent_->setNoteHeight(noteHeight_);
    if (keyboard_)
        keyboard_->setNoteHeight(noteHeight_);
    if (octaveLabelStrip_)
        octaveLabelStrip_->setNoteHeight(noteHeight_);

    updateGridSize();

    if (persist && editingClipId_ != magda::INVALID_CLIP_ID) {
        magda::ClipManager::getInstance().setClipMidiEditorRowHeight(editingClipId_, noteHeight_);
    }
}

void PianoRollContent::setNoteHeightAnchored(int height, int anchorNote, int anchorScreenY,
                                             bool persist) {
    const int previousHeight = noteHeight_;
    setNoteHeight(height, persist);
    if (noteHeight_ == previousHeight || !viewport_)
        return;

    const int newAnchorY = gridComponent_ ? gridComponent_->noteNumberToY(anchorNote)
                                          : (MAX_NOTE - anchorNote) * noteHeight_;
    const int newScrollY = juce::jmax(0, newAnchorY - anchorScreenY);
    viewport_->setViewPosition(viewport_->getViewPositionX(), newScrollY);
}

void PianoRollContent::loadNoteHeightFromClip(magda::ClipId clipId) {
    const auto* clip = magda::ClipManager::getInstance().getClip(clipId);
    if (clip && clip->isMidi()) {
        setNoteHeight(
            clip->midiEditorRowHeight > 0 ? clip->midiEditorRowHeight : DEFAULT_NOTE_HEIGHT, false);
    }
}

void PianoRollContent::highlightMonitoredNote(int noteNumber, bool noteOn) {
    if (keyboard_)
        keyboard_->setNotePressed(noteNumber, noteOn);
}

void PianoRollContent::ensureMonitoredNoteVisible(int noteNumber) {
    ensureNoteVisible(noteNumber);
}

void PianoRollContent::setupGridCallbacks() {
    // Handle note addition
    gridComponent_->onNoteAdded = [](magda::ClipId clipId, double beat, int noteNumber,
                                     double lengthBeats, int velocity) {
        auto cmd = std::make_unique<magda::AddMidiNoteCommand>(clipId, beat, noteNumber,
                                                               lengthBeats, velocity);
        magda::UndoManager::getInstance().executeCommand(std::move(cmd));
        // Note: UI refresh handled via ClipManagerListener::clipPropertyChanged()
    };

    // Handle pitch glide edits (MPE pitch expression overlay)
    gridComponent_->onPitchExpressionChanged =
        [](magda::ClipId clipId, size_t noteIndex,
           std::vector<magda::MidiPitchExpressionPoint> points) {
            auto cmd = std::make_unique<magda::SetNotePitchExpressionCommand>(clipId, noteIndex,
                                                                              std::move(points));
            magda::UndoManager::getInstance().executeCommand(std::move(cmd));
        };

    // Handle note movement
    gridComponent_->onNoteMoved = [](magda::ClipId clipId, size_t noteIndex, double newBeat,
                                     int newNoteNumber) {
        // Get source clip and note
        auto* sourceClip = magda::ClipManager::getInstance().getClip(clipId);
        if (!sourceClip || noteIndex >= sourceClip->midiNotes.size())
            return;

        // Normal movement within same clip (only executed if no cross-clip transfer occurred)
        auto cmd =
            std::make_unique<magda::MoveMidiNoteCommand>(clipId, noteIndex, newBeat, newNoteNumber);
        magda::UndoManager::getInstance().executeCommand(std::move(cmd));

        // After moving, check if note is still visible in this clip (considering offset)
        auto* clip = magda::ClipManager::getInstance().getClip(clipId);
        if (clip && clip->isMidi() && noteIndex < clip->midiNotes.size()) {
            const auto& note = clip->midiNotes[noteIndex];
            double tempo = 120.0;
            if (auto* controller = magda::TimelineController::getCurrent()) {
                tempo = controller->getState().tempo.bpm;
            }
            double beatsPerSecond = tempo / 60.0;
            double clipLengthBeats = clip->placement.lengthBeats;

            // Check if note is outside visible range [offset, offset + length]
            double effectiveOffset = (clip->view == magda::ClipView::Session || clip->loopEnabled)
                                         ? clip->midiOffset
                                         : 0.0;
            if (note.startBeat < effectiveOffset ||
                note.startBeat >= effectiveOffset + clipLengthBeats) {
                DBG("Note is no longer visible in clip "
                    << clipId << " (offset=" << clip->midiOffset << ", note at " << note.startBeat
                    << ")");

                // Find which clip would show this note
                // Note: startBeat is in content coordinates, so subtract offset to get timeline
                // position
                double clipStartBeats = clip->placement.startBeat;
                double absoluteBeat = clipStartBeats + note.startBeat - effectiveOffset;
                double absoluteSeconds = absoluteBeat / beatsPerSecond;

                magda::ClipId destClipId = magda::ClipManager::getInstance().getClipAtPosition(
                    clip->trackId, absoluteSeconds);

                if (destClipId != magda::INVALID_CLIP_ID && destClipId != clipId) {
                    DBG("  -> Would be visible in clip " << destClipId << ", moving it there");
                    auto* destClip = magda::ClipManager::getInstance().getClip(destClipId);
                    if (destClip && destClip->isMidi()) {
                        // Calculate position in destination clip's content coordinates
                        // absoluteBeat is timeline position, convert to content position
                        double destClipStartBeats = destClip->placement.startBeat;
                        double destOffset =
                            (destClip->view == magda::ClipView::Session || destClip->loopEnabled)
                                ? destClip->midiOffset
                                : 0.0;
                        double relativeNewBeat = absoluteBeat - destClipStartBeats + destOffset;

                        DBG("  -> Transfer: absoluteBeat="
                            << absoluteBeat << ", destClipStart=" << destClipStartBeats
                            << ", destOffset=" << destClip->midiOffset
                            << ", contentBeat=" << relativeNewBeat);

                        // Move to destination clip
                        auto moveCmd = std::make_unique<magda::MoveMidiNoteBetweenClipsCommand>(
                            clipId, noteIndex, destClipId, relativeNewBeat, note.noteNumber);
                        magda::UndoManager::getInstance().executeCommand(std::move(moveCmd));
                    }
                }
            }
        }
        // Note: UI refresh handled via ClipManagerListener::clipPropertyChanged()
    };

    // Handle note copy (shift+drag)
    gridComponent_->onNoteCopied = [this](magda::ClipId clipId, size_t noteIndex, double destBeat,
                                          int destNoteNumber) {
        const auto* clip = magda::ClipManager::getInstance().getClip(clipId);
        if (!clip || noteIndex >= clip->midiNotes.size())
            return;

        const auto& sourceNote = clip->midiNotes[noteIndex];
        auto cmd = std::make_unique<magda::AddMidiNoteCommand>(
            clipId, destBeat, destNoteNumber, sourceNote.lengthBeats, sourceNote.velocity);
        magda::UndoManager::getInstance().executeCommand(std::move(cmd));

        // Select the newly copied note (appended at end) after the async refresh
        const auto* updatedClip = magda::ClipManager::getInstance().getClip(clipId);
        if (updatedClip && !updatedClip->midiNotes.empty()) {
            int newNoteIndex = static_cast<int>(updatedClip->midiNotes.size()) - 1;
            gridComponent_->selectNoteAfterRefresh(clipId, newNoteIndex);
        }
    };

    // Handle note resizing
    gridComponent_->onNoteResized = [](magda::ClipId clipId, size_t noteIndex, double newLength) {
        auto cmd = std::make_unique<magda::ResizeMidiNoteCommand>(clipId, noteIndex, newLength);
        magda::UndoManager::getInstance().executeCommand(std::move(cmd));
        // Note: UI refresh handled via ClipManagerListener::clipPropertyChanged()
    };

    // Handle note deletion
    gridComponent_->onNoteDeleted = [](magda::ClipId clipId, size_t noteIndex) {
        auto cmd = std::make_unique<magda::DeleteMidiNoteCommand>(clipId, noteIndex);
        magda::UndoManager::getInstance().executeCommand(std::move(cmd));
        // Note: UI refresh handled via ClipManagerListener::clipPropertyChanged()
    };

    // Handle batch note movement (single undo step)
    gridComponent_->onMultipleNotesMoved =
        [](magda::ClipId clipId, std::vector<magda::MoveMultipleMidiNotesCommand::NoteMove> moves) {
            auto cmd =
                std::make_unique<magda::MoveMultipleMidiNotesCommand>(clipId, std::move(moves));
            magda::UndoManager::getInstance().executeCommand(std::move(cmd));
        };

    // Handle batch note resize (single undo step)
    gridComponent_->onMultipleNotesResized =
        [](magda::ClipId clipId, std::vector<std::pair<size_t, double>> noteLengths) {
            auto cmd = std::make_unique<magda::ResizeMultipleMidiNotesCommand>(
                clipId, std::move(noteLengths));
            magda::UndoManager::getInstance().executeCommand(std::move(cmd));
        };

    // Handle left-edge resize for multi-selection (compound move+resize as single undo step)
    gridComponent_->onLeftResizeMultipleNotes =
        [](magda::ClipId clipId, std::vector<magda::MoveMultipleMidiNotesCommand::NoteMove> moves,
           std::vector<std::pair<size_t, double>> noteLengths) {
            magda::CompoundOperationScope scope("Resize Notes From Left");
            auto moveCmd =
                std::make_unique<magda::MoveMultipleMidiNotesCommand>(clipId, std::move(moves));
            magda::UndoManager::getInstance().executeCommand(std::move(moveCmd));
            auto resizeCmd = std::make_unique<magda::ResizeMultipleMidiNotesCommand>(
                clipId, std::move(noteLengths));
            magda::UndoManager::getInstance().executeCommand(std::move(resizeCmd));
        };

    // Handle note selection - update SelectionManager
    gridComponent_->onNoteSelected = [](magda::ClipId clipId, size_t noteIndex, bool isAdditive) {
        if (isAdditive) {
            magda::SelectionManager::getInstance().addNoteToSelection(clipId, noteIndex);
        } else {
            magda::SelectionManager::getInstance().selectNote(clipId, noteIndex);
        }
    };

    gridComponent_->onNoteRangeSelected = [](magda::ClipId clipId, size_t noteIndex) {
        magda::SelectionManager::getInstance().extendNoteSelectionTo(clipId, noteIndex);
    };

    // Handle batch note selection changes (lasso, deselect-all, Cmd+click toggle)
    gridComponent_->onNoteSelectionChanged = [this](magda::ClipId clipId,
                                                    std::vector<size_t> noteIndices) {
        if (noteIndices.empty()) {
            // Clear note selection — preserve clip selection
            magda::SelectionManager::getInstance().clearNoteSelection();
            if (midiDrawer_ && midiDrawer_->getVelocityLane()) {
                midiDrawer_->getVelocityLane()->setSelectedNoteIndices({});
            } else if (velocityLane_) {
                velocityLane_->setSelectedNoteIndices({});
            }
        } else {
            magda::SelectionManager::getInstance().selectNotes(clipId, noteIndices);
        }
    };

    // Forward note drag preview to velocity lane for position sync
    gridComponent_->onNoteDragging = [this](magda::ClipId /*clipId*/, size_t noteIndex,
                                            double previewBeat, bool isDragging) {
        if (midiDrawer_ && midiDrawer_->getVelocityLane()) {
            midiDrawer_->getVelocityLane()->setNotePreviewPosition(noteIndex, previewBeat,
                                                                   isDragging);
        } else if (velocityLane_) {
            velocityLane_->setNotePreviewPosition(noteIndex, previewBeat, isDragging);
        }
    };

    // Handle quantize from right-click context menu
    gridComponent_->onQuantizeNotes = [](magda::ClipId clipId, std::vector<size_t> noteIndices,
                                         magda::QuantizeMode mode, double gridBeats) {
        auto cmd = std::make_unique<magda::QuantizeMidiNotesCommand>(clipId, std::move(noteIndices),
                                                                     gridBeats, mode);
        magda::UndoManager::getInstance().executeCommand(std::move(cmd));
    };

    // Handle copy from context menu
    gridComponent_->onCopyNotes = [](magda::ClipId clipId, std::vector<size_t> noteIndices) {
        magda::ClipManager::getInstance().copyNotesToClipboard(clipId, noteIndices);
    };

    // Handle paste from context menu
    gridComponent_->onPasteNotes = [](magda::ClipId clipId) {
        auto& clipManager = magda::ClipManager::getInstance();
        if (!clipManager.hasNotesInClipboard())
            return;

        const auto* clip = clipManager.getClip(clipId);
        if (!clip || !clip->isMidi())
            return;

        double pasteOffset = clipManager.getNoteClipboardMinBeat();
        const auto& clipboard = clipManager.getNoteClipboard();
        std::vector<magda::MidiNote> notesToPaste;
        notesToPaste.reserve(clipboard.size());
        for (const auto& note : clipboard) {
            magda::MidiNote n = note;
            n.startBeat += pasteOffset;
            notesToPaste.push_back(n);
        }

        auto cmd = std::make_unique<magda::AddMultipleMidiNotesCommand>(
            clipId, std::move(notesToPaste), "Paste MIDI Notes");
        auto* cmdPtr = cmd.get();
        magda::UndoManager::getInstance().executeCommand(std::move(cmd));

        const auto& inserted = cmdPtr->getInsertedIndices();
        if (!inserted.empty()) {
            magda::SelectionManager::getInstance().selectNotes(
                clipId, std::vector<size_t>(inserted.begin(), inserted.end()));
        }
    };

    // Handle duplicate from context menu
    gridComponent_->onDuplicateNotes = [this](magda::ClipId clipId,
                                              std::vector<size_t> noteIndices) {
        auto& clipManager = magda::ClipManager::getInstance();
        const auto* clip = clipManager.getClip(clipId);
        if (!clip || !clip->isMidi())
            return;

        double minStart = std::numeric_limits<double>::max();
        double maxEnd = 0.0;
        std::vector<magda::MidiNote> notesToDuplicate;
        for (size_t idx : noteIndices) {
            if (idx < clip->midiNotes.size()) {
                const auto& note = clip->midiNotes[idx];
                notesToDuplicate.push_back(note);
                minStart = std::min(minStart, note.startBeat);
                maxEnd = std::max(maxEnd, note.startBeat + note.lengthBeats);
            }
        }
        if (!notesToDuplicate.empty()) {
            double offset = maxEnd - minStart;
            for (auto& note : notesToDuplicate) {
                note.startBeat += offset;
            }
            auto cmd = std::make_unique<magda::AddMultipleMidiNotesCommand>(
                clipId, std::move(notesToDuplicate), "Duplicate MIDI Notes");
            auto* cmdPtr = cmd.get();
            magda::UndoManager::getInstance().executeCommand(std::move(cmd));

            const auto& inserted = cmdPtr->getInsertedIndices();
            if (!inserted.empty()) {
                magda::SelectionManager::getInstance().selectNotes(
                    clipId, std::vector<size_t>(inserted.begin(), inserted.end()));
                gridComponent_->syncSelectionFromManager();
            }
        }
    };

    // Handle delete from context menu
    gridComponent_->onDeleteNotes = [](magda::ClipId clipId, std::vector<size_t> noteIndices) {
        auto cmd =
            std::make_unique<magda::DeleteMultipleMidiNotesCommand>(clipId, std::move(noteIndices));
        magda::UndoManager::getInstance().executeCommand(std::move(cmd));
        magda::SelectionManager::getInstance().clearNoteSelection();
    };

    // Edit cursor set from grid (Alt+click on grid line) — local to MIDI editor
    gridComponent_->onEditCursorSet = [this](double positionSeconds) {
        setLocalEditCursor(positionSeconds);
    };

    // Playhead set from grid — global arrangement transport, matching the timeline ruler.
    gridComponent_->onPlayheadPositionBeatsChanged = [](double positionBeats) {
        if (auto* controller = magda::TimelineController::getCurrent()) {
            const auto& state = controller->getState();
            controller->dispatch(
                magda::SetPlayheadPositionBeatsEvent{state.snapBeatsToGrid(positionBeats)});
        }
    };

    // Handle chord block drops from the chord panel
    gridComponent_->onChordDropped = [](magda::ClipId clipId, double beat, double noteLength,
                                        std::vector<std::pair<int, int>> notes,
                                        juce::String chordName) {
        if (notes.empty())
            return;

        // Allocate chord group ID for linking notes to annotation
        auto* clipData = magda::ClipManager::getInstance().getClip(clipId);
        int groupId = clipData ? clipData->nextChordGroupId++ : 0;

        // Compound: MIDI notes + chord annotation undo as one step
        magda::CompoundOperationScope scope("Add Chord");

        std::vector<magda::MidiNote> midiNotes;
        midiNotes.reserve(notes.size());
        for (const auto& [noteNumber, velocity] : notes) {
            magda::MidiNote mn;
            mn.noteNumber = noteNumber;
            mn.velocity = velocity;
            mn.startBeat = beat;
            mn.lengthBeats = noteLength;
            mn.chordGroup = groupId;
            midiNotes.push_back(mn);
        }

        auto noteCmd = std::make_unique<magda::AddMultipleMidiNotesCommand>(
            clipId, std::move(midiNotes), "Add chord notes");
        magda::UndoManager::getInstance().executeCommand(std::move(noteCmd));

        if (chordName.isNotEmpty()) {
            magda::ClipInfo::ChordAnnotation annotation;
            annotation.beatPosition = beat;
            annotation.lengthBeats = noteLength;
            annotation.chordName = chordName;
            annotation.chordGroup = groupId;
            auto chordCmd = std::make_unique<magda::AddChordAnnotationCommand>(clipId, annotation);
            magda::UndoManager::getInstance().executeCommand(std::move(chordCmd));
        }
    };
}

// ============================================================================
// MidiEditorContent virtual implementations
// ============================================================================

void PianoRollContent::setGridPixelsPerBeat(double ppb) {
    if (gridComponent_)
        gridComponent_->setPixelsPerBeat(ppb);
    if (takeLanes_)
        takeLanes_->setPixelsPerBeat(ppb);
    if (showChordRow_)
        repaint();
}

void PianoRollContent::setGridPlayheadPosition(double position) {
    if (gridComponent_)
        gridComponent_->setPlayheadPosition(position);
    // The chord-band playhead is painted by this component (over the chord row),
    // so repaint that strip to keep it in step with the grid's playhead.
    if (showChordRow_)
        repaint(chordLaneLeftX(), chordRowTop(), getWidth() - chordLaneLeftX(), chordRowHeight());
}

void PianoRollContent::setGridEditCursorPosition(double pos, bool visible) {
    if (gridComponent_)
        gridComponent_->setEditCursorPosition(pos, visible);
}

void PianoRollContent::onScrollPositionChanged(int scrollX, int scrollY) {
    keyboard_->setScrollOffset(scrollY);
    if (octaveLabelStrip_)
        octaveLabelStrip_->setScrollOffset(scrollY);
    if (midiDrawer_) {
        midiDrawer_->setScrollOffset(scrollX);
    } else if (velocityLane_) {
        velocityLane_->setScrollOffset(scrollX);
    }
    if (takeLanes_)
        takeLanes_->setScrollOffset(scrollX);
}

void PianoRollContent::onGridResolutionChanged() {
    if (gridComponent_) {
        gridComponent_->setGridResolutionBeats(gridResolutionBeats_);
        gridComponent_->setSnapEnabled(snapEnabled_);

        // Sync time signature
        if (auto* controller = magda::TimelineController::getCurrent()) {
            gridComponent_->setTimeSignatureNumerator(
                controller->getState().tempo.timeSignatureNumerator);
        }
    }
    if (timeRuler_)
        timeRuler_->setGridResolution(gridResolutionBeats_);
}

// ============================================================================
// Paint / Layout
// ============================================================================

void PianoRollContent::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getPanelBackgroundColour());

    if (getWidth() <= 0 || getHeight() <= 0)
        return;

    // Draw sidebar on the left (chord-focus mode drops it entirely)
    if (sidebarWidth() > 0) {
        auto sidebarArea = getLocalBounds().removeFromLeft(sidebarWidth());
        drawSidebar(g, sidebarArea);
    }

    // Draw chord row below the ruler (if visible)
    if (showChordRow_) {
        auto chordArea = getLocalBounds();
        chordArea.removeFromLeft(sidebarWidth());
        chordArea.removeFromTop(chordRowTop());  // ruler occupies the top band
        chordArea = chordArea.removeFromTop(chordRowHeight());
        chordArea.removeFromLeft(ZOOM_STRIP_WIDTH + OCTAVE_LABEL_WIDTH + KEYBOARD_WIDTH);
        drawChordRow(g, chordArea);

        // Horizontal separator at bottom of chord row — full width
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.drawHorizontalLine(chordRowTop() + chordRowHeight() - 1,
                             static_cast<float>(sidebarWidth()), static_cast<float>(getWidth()));
    }

    // Draw velocity drawer header (if open) — only for legacy path without MidiDrawer
    if (velocityDrawerOpen_ && !midiDrawer_) {
        auto drawerHeaderArea = getLocalBounds();
        drawerHeaderArea.removeFromLeft(sidebarWidth());
        drawerHeaderArea = drawerHeaderArea.removeFromBottom(drawerHeight_);
        drawerHeaderArea = drawerHeaderArea.removeFromTop(VELOCITY_HEADER_HEIGHT);
        drawVelocityHeader(g, drawerHeaderArea);
    }
}

void PianoRollContent::paintOverChildren(juce::Graphics& g) {
    // The ruler now sits at the very top; extend its tick-area border line
    // through the sidebar/keyboard corner.
    int tickLineY = RULER_HEIGHT - LayoutConfig::getInstance().rulerMajorTickHeight;
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.fillRect(sidebarWidth(), tickLineY, ZOOM_STRIP_WIDTH + OCTAVE_LABEL_WIDTH + KEYBOARD_WIDTH,
               1);

    // Playhead over the chord blocks. The grid only draws the playhead across
    // the note grid, so mirror it in the chord-row band using the exact x the
    // grid drew (incl. loop wrap) so the two stay locked while scrolling.
    if (showChordRow_ && gridComponent_ && viewport_) {
        int gridX = 0;
        if (gridComponent_->getPlayheadDisplayX(gridX)) {
            const int contentX = viewport_->getX() + gridX - viewport_->getViewPositionX();
            if (contentX >= chordLaneLeftX() && contentX <= getWidth()) {
                g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
                g.fillRect(contentX - 1, chordRowTop(), 2, chordRowHeight());
            }
        }
    }
}

void PianoRollContent::resized() {
    auto bounds = getLocalBounds();

    // Skip sidebar (painted in paint())
    bounds.removeFromLeft(sidebarWidth());

    // Position sidebar icons. Chord-focus mode drops the whole strip, so every
    // toggle that lives in it is hidden.
    const bool hasSidebar = sidebarWidth() > 0;
    int iconSize = 22;
    int padding = (sidebarWidth() - iconSize) / 2;
    // Chord toggle in the sidebar — vertically centered in the chord-row band
    int chordToggleY = showChordRow_ ? chordRowTop() + (chordRowHeight() - iconSize) / 2 : padding;
    chordToggle_->setVisible(hasSidebar);
    chordToggle_->setBounds(padding, chordToggleY, iconSize, iconSize);
    // Fold toggle directly below the chord toggle
    if (foldToggle_) {
        foldToggle_->setVisible(hasSidebar);
        foldToggle_->setBounds(padding, chordToggleY + iconSize + padding, iconSize, iconSize);
    }
    // Take-lanes toggle below the fold toggle (only when the clip has takes)
    if (takeLanesToggle_) {
        const auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
        const bool hasTakes =
            hasSidebar && clip != nullptr && clip->isMidi() && clip->midi().takes.size() >= 2;
        takeLanesToggle_->setVisible(hasTakes);
        if (hasTakes) {
            takeLanesToggle_->setActive(clip->takesExpanded);
            takeLanesToggle_->setBounds(padding, chordToggleY + 2 * (iconSize + padding), iconSize,
                                        iconSize);
        }
    }
    // Lane buttons stacked at the bottom, top to bottom: MPE, CC, velocity.
    // Chord-focus mode hides them entirely - a chord clip has no per-note
    // velocity/CC editing surface.
    const bool laneTogglesVisible = hasSidebar && !chordFocusMode();
    velocityToggle_->setVisible(laneTogglesVisible);
    ccLanesBtn_->setVisible(laneTogglesVisible);
    pitchGlideToggle_->setVisible(laneTogglesVisible);
    velocityToggle_->setBounds(padding, getHeight() - iconSize - padding, iconSize, iconSize);
    ccLanesBtn_->setBounds(padding, getHeight() - 2 * (iconSize + padding), iconSize, iconSize);
    pitchGlideToggle_->setBounds(padding, getHeight() - 3 * (iconSize + padding), iconSize,
                                 iconSize);

    // Ruler row at the very top, above the chord lane.
    {
        auto rulerArea = bounds.removeFromTop(RULER_HEIGHT);
        rulerArea.removeFromLeft(ZOOM_STRIP_WIDTH + OCTAVE_LABEL_WIDTH + KEYBOARD_WIDTH);
        timeRuler_->setBounds(rulerArea);
    }

    // Chord lane sits directly below the ruler (drawn in paint).
    if (showChordRow_) {
        bounds.removeFromTop(chordRowHeight());
        const int detectSize = 18;
        const bool chordMode = chordFocusMode();

        chordDetectBtn_->setVisible(!chordMode);
        gridToggleBtn_->setVisible(chordMode);

        // The overlay toggle is only useful on a non-chord track when a chord
        // track exists to overlay from.
        bool onChordTrack = false;
        if (const auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_)) {
            const auto* tr = magda::TrackManager::getInstance().getTrack(clip->trackId);
            onChordTrack = tr != nullptr && tr->type == magda::TrackType::Chord;
        }
        const bool overlayApplies =
            !chordMode && !onChordTrack && magda::TrackManager::getInstance().hasChordTrack();
        progressionOverlayToggle_->setVisible(overlayApplies);
        progressionOverlayToggle_->setActive(showProgressionOverlay_);

        if (chordMode) {
            // Grid show/hide toggle: right side of the ruler row at the top (the
            // ruler stays visible when the grid is hidden).
            const int gridSize = 14;
            const int rightMargin = 6;
            const int gx = chordLaneLeftX() - gridSize - rightMargin;
            const int gy = (RULER_HEIGHT - gridSize) / 2;
            gridToggleBtn_->setBounds(gx, gy, gridSize, gridSize);
            gridToggleBtn_->setActive(gridShown());  // accent when the grid is visible
        } else {
            // Rescan button: keyboard column, vertically centred in the chord row.
            const int detectX =
                sidebarWidth() + ZOOM_STRIP_WIDTH + (KEYBOARD_WIDTH - detectSize) / 2;
            const int detectY = chordRowTop() + (chordRowHeight() - detectSize) / 2;
            chordDetectBtn_->setBounds(detectX, detectY, detectSize, detectSize);
            if (overlayApplies)
                progressionOverlayToggle_->setBounds(detectX + detectSize + 2, detectY, detectSize,
                                                     detectSize);
        }
    } else {
        chordDetectBtn_->setVisible(false);
        gridToggleBtn_->setVisible(false);
        progressionOverlayToggle_->setVisible(false);
    }

    // MIDI drawer at bottom (if open). Suppressed in chord-focus mode.
    if (velocityDrawerOpen_ && !chordFocusMode()) {
        auto drawerArea = bounds.removeFromBottom(drawerHeight_);
        if (midiDrawer_) {
            // MidiDrawerComponent gets the full width including the left column,
            // so it can place controls (e.g. PB range) in the left margin area.
            midiDrawer_->setLeftMargin(ZOOM_STRIP_WIDTH + OCTAVE_LABEL_WIDTH + KEYBOARD_WIDTH);
            midiDrawer_->setBounds(drawerArea);
            midiDrawer_->setVisible(true);
        } else if (velocityLane_) {
            // Legacy path: separate header drawn in paint(), lane below
            drawerArea.removeFromTop(VELOCITY_HEADER_HEIGHT);
            drawerArea.removeFromLeft(ZOOM_STRIP_WIDTH + OCTAVE_LABEL_WIDTH + KEYBOARD_WIDTH);
            velocityLane_->setBounds(drawerArea);
            velocityLane_->setVisible(true);
        }
    } else {
        if (midiDrawer_)
            midiDrawer_->setVisible(false);
        if (velocityLane_)
            velocityLane_->setVisible(false);
    }

    // Folded take-lanes strip (MIDI comping) — directly above the drawer, below
    // the grid, aligned to the grid's time axis.
    if (takeLanesVisible() && takeLanes_) {
        int stripH =
            juce::jmin(takeLanes_->preferredHeight(), juce::jmax(0, bounds.getHeight() / 2));
        auto stripArea = bounds.removeFromBottom(stripH);
        // Span the octave-label + keyboard columns too, used as a fixed left
        // gutter for the take name (aligned with the keyboard).
        stripArea.removeFromLeft(ZOOM_STRIP_WIDTH);
        takeLanes_->setLabelGutter(OCTAVE_LABEL_WIDTH + KEYBOARD_WIDTH);
        takeLanes_->setBounds(stripArea);
        takeLanes_->setVisible(true);
        refreshTakeLanes();
    } else if (takeLanes_) {
        takeLanes_->setVisible(false);
    }

    auto zoomStripArea = bounds.removeFromLeft(ZOOM_STRIP_WIDTH);
    verticalZoomStrip_->setBounds(zoomStripArea);

    // Octave label strip between the zoom strip and the keyboard.
    auto octaveStripArea = bounds.removeFromLeft(OCTAVE_LABEL_WIDTH);
    if (octaveLabelStrip_)
        octaveLabelStrip_->setBounds(octaveStripArea);

    // Keyboard on the left
    auto keyboardArea = bounds.removeFromLeft(KEYBOARD_WIDTH);
    keyboard_->setBounds(keyboardArea);

    // Viewport fills the remaining space
    viewport_->setBounds(bounds);

    // Update the grid size
    updateGridSize();
    updateTimeRuler();
    updateVelocityLane();

    // Center on notes (or C4) on first layout
    if (needsInitialCentering_ && viewport_->getHeight() > 0) {
        centerOnNotes();
        needsInitialCentering_ = false;
    }
}

// ============================================================================
// Mouse
// ============================================================================

void PianoRollContent::mouseDown(const juce::MouseEvent& e) {
    // Chord lane click: offer it to the subclass hook (chord-clip add). The
    // standard piano roll's hook returns false, so the event falls through.
    if (showChordRow_ && e.y >= chordRowTop() && e.y < chordRowTop() + chordRowHeight()) {
        const int leftPanelWidth =
            sidebarWidth() + ZOOM_STRIP_WIDTH + OCTAVE_LABEL_WIDTH + KEYBOARD_WIDTH;
        if (e.x >= leftPanelWidth && horizontalZoom_ > 0.0) {
            if (onChordRowClicked(chordRowBeatForX(e.x)))
                return;
        }
    }

    MidiEditorContent::mouseDown(e);
}

double PianoRollContent::chordRowBeatForX(int x) const {
    if (horizontalZoom_ <= 0.0)
        return 0.0;
    const int leftPanelWidth =
        sidebarWidth() + ZOOM_STRIP_WIDTH + OCTAVE_LABEL_WIDTH + KEYBOARD_WIDTH;
    const int scrollX = viewport_ ? viewport_->getViewPositionX() : 0;
    const double absBeat = (x - leftPanelWidth + scrollX - GRID_LEFT_PADDING) / horizontalZoom_;
    double clipStartBeats = 0.0;
    if (const auto* clip = (editingClipId_ != magda::INVALID_CLIP_ID)
                               ? magda::ClipManager::getInstance().getClip(editingClipId_)
                               : nullptr) {
        if (!relativeTimeMode_ && clip->view != magda::ClipView::Session)
            clipStartBeats = clip->placement.startBeat;
    }
    return juce::jmax(0.0, absBeat - clipStartBeats);
}

int PianoRollContent::chordRowXForBeat(double clipRelativeBeat) const {
    const int leftPanelWidth =
        sidebarWidth() + ZOOM_STRIP_WIDTH + OCTAVE_LABEL_WIDTH + KEYBOARD_WIDTH;
    const int scrollX = viewport_ ? viewport_->getViewPositionX() : 0;
    double clipStartBeats = 0.0;
    if (const auto* clip = (editingClipId_ != magda::INVALID_CLIP_ID)
                               ? magda::ClipManager::getInstance().getClip(editingClipId_)
                               : nullptr) {
        if (!relativeTimeMode_ && clip->view != magda::ClipView::Session)
            clipStartBeats = clip->placement.startBeat;
    }
    const double absBeat = clipRelativeBeat + clipStartBeats;
    return leftPanelWidth + static_cast<int>(absBeat * horizontalZoom_) + GRID_LEFT_PADDING -
           scrollX;
}

void PianoRollContent::redetectChords() {
    detectChordsFromNotes();
}

void PianoRollContent::setGridToggleActive(bool on) {
    if (gridToggleBtn_)
        gridToggleBtn_->setActive(on);
}

void PianoRollContent::mouseWheelMove(const juce::MouseEvent& e,
                                      const juce::MouseWheelDetails& wheel) {
    int headerHeight = getHeaderHeight();
    int leftPanelWidth = sidebarWidth() + ZOOM_STRIP_WIDTH + OCTAVE_LABEL_WIDTH + KEYBOARD_WIDTH;
    const auto gesture = magda::GestureRouter::getInstance().resolve(
        magda::GestureContext::PianoRoll, wheel, e.mods, e.getPosition());

    // Check if mouse is over the chord row band (below the ruler, when visible)
    if (showChordRow_ && e.y >= chordRowTop() && e.y < chordRowTop() + chordRowHeight() &&
        e.x >= leftPanelWidth) {
        if (gesture.type == magda::GestureActionType::ScrollHorizontal &&
            timeRuler_->onScrollRequested) {
            int scrollAmount = static_cast<int>(-gesture.magnitude);
            if (scrollAmount != 0)
                timeRuler_->onScrollRequested(scrollAmount);
        }
        return;
    }

    // Check if mouse is over the time ruler area (very top)
    if (e.y < RULER_HEIGHT && e.x >= leftPanelWidth) {
        if (gesture.type == magda::GestureActionType::ScrollHorizontal &&
            timeRuler_->onScrollRequested) {
            int scrollAmount = static_cast<int>(-gesture.magnitude);
            if (scrollAmount != 0)
                timeRuler_->onScrollRequested(scrollAmount);
        }
        return;
    }

    // Check if mouse is over the keyboard area (left side, below header)
    if (e.x >= sidebarWidth() + ZOOM_STRIP_WIDTH && e.x < leftPanelWidth && e.y >= headerHeight) {
        if (gesture.type == magda::GestureActionType::ScrollVertical &&
            keyboard_->onScrollRequested) {
            int scrollAmount = static_cast<int>(-gesture.magnitude);
            if (scrollAmount != 0)
                keyboard_->onScrollRequested(scrollAmount);
        }
        return;
    }

    if (gesture.type == magda::GestureActionType::ScrollHorizontal && viewport_) {
        viewport_->setViewPosition(viewport_->getViewPositionX() -
                                       static_cast<int>(gesture.magnitude),
                                   viewport_->getViewPositionY());
        return;
    }

    if (gesture.type == magda::GestureActionType::ScrollVertical && viewport_) {
        viewport_->setViewPosition(viewport_->getViewPositionX(),
                                   viewport_->getViewPositionY() -
                                       static_cast<int>(gesture.magnitude));
        return;
    }

    if (gesture.type == magda::GestureActionType::ZoomHorizontal) {
        double zoomFactor = std::pow(2.0, static_cast<double>(gesture.magnitude));
        int mouseXInViewport = e.x - leftPanelWidth;
        performWheelZoom(zoomFactor, mouseXInViewport);
        return;
    }

    if (gesture.type == magda::GestureActionType::ZoomVertical) {
        int mouseYInContent = e.y - headerHeight + viewport_->getViewPositionY();
        int anchorNote = MAX_NOTE - (mouseYInContent / noteHeight_);

        const int heightDelta = gesture.magnitude > 0.0f ? 2 : -2;
        setNoteHeightAnchored(noteHeight_ + heightDelta, anchorNote, e.y - headerHeight, true);
        return;
    }

    // Regular scroll - don't handle, let default JUCE event propagation work
    // (The viewport will receive the event through normal component hierarchy)
}

// ============================================================================
// Grid sizing (PianoRoll-specific)
// ============================================================================

void PianoRollContent::updateGridSize() {
    auto& clipManager = magda::ClipManager::getInstance();
    const auto* clip =
        editingClipId_ != magda::INVALID_CLIP_ID ? clipManager.getClip(editingClipId_) : nullptr;

    // Get tempo to convert between seconds and beats
    double tempo = 120.0;
    double timelineLength = 300.0;  // Default 5 minutes
    if (auto* controller = magda::TimelineController::getCurrent()) {
        const auto& state = controller->getState();
        tempo = state.tempo.bpm;
        timelineLength = state.timelineLength;
    }
    double secondsPerBeat = 60.0 / tempo;

    // Always use the full arrangement length for the grid
    double displayLengthBeats = timelineLength / secondsPerBeat;

    // Calculate clip position and length in beats
    double clipStartBeats = 0.0;
    double clipLengthBeats = 0.0;

    // When multiple clips are selected, compute the combined range
    const auto& selectedClipIds = gridComponent_->getSelectedClipIds();
    if (selectedClipIds.size() > 1) {
        double earliestStartBeat = std::numeric_limits<double>::max();
        double latestEndBeat = 0.0;
        for (magda::ClipId id : selectedClipIds) {
            const auto* c = clipManager.getClip(id);
            if (!c)
                continue;
            earliestStartBeat = juce::jmin(earliestStartBeat, c->placement.startBeat);
            latestEndBeat = juce::jmax(latestEndBeat, c->placement.endBeat());
        }
        clipStartBeats = earliestStartBeat;
        clipLengthBeats = latestEndBeat - earliestStartBeat;
    } else if (clip) {
        if (clip->loopEnabled || clip->view == magda::ClipView::Session) {
            // Looped clips and session clips: show content from bar 1
            clipStartBeats = 0.0;
            clipLengthBeats = clip->placement.lengthBeats;
        } else {
            clipStartBeats = clip->placement.startBeat;
            clipLengthBeats = clip->placement.lengthBeats;
        }
    }

    // Refresh the folded-pitch axis before sizing — the row count drives height.
    rebuildFoldMap();

    int gridWidth = juce::jmax(viewport_->getWidth(),
                               static_cast<int>(displayLengthBeats * horizontalZoom_) + 100);
    int gridHeight = foldMap_.rowCount() * noteHeight_;

    gridComponent_->setSize(gridWidth, gridHeight);

    gridComponent_->setRelativeMode(relativeTimeMode_);
    gridComponent_->setClipStartBeats(clipStartBeats);
    gridComponent_->setClipLengthBeats(clipLengthBeats);
    gridComponent_->setTimelineLengthBeats(displayLengthBeats);

    // Pass loop region data to grid
    if (clip && selectedClipIds.size() <= 1) {
        double tempo = 120.0;
        if (auto* controller = magda::TimelineController::getCurrent()) {
            tempo = controller->getState().tempo.bpm;
        }
        double beatsPerSecond = tempo / 60.0;
        double loopOffsetBeats = clip->loopStart * beatsPerSecond;
        // MIDI clips use loopLengthBeats directly; audio clips derive from loopLength (seconds)
        double sourceLengthBeats =
            clip->loopLengthBeats > 0.0 ? clip->loopLengthBeats : clip->loopLength * beatsPerSecond;
        gridComponent_->setLoopRegion(loopOffsetBeats, sourceLengthBeats, clip->loopEnabled);
    } else {
        gridComponent_->setLoopRegion(0.0, 0.0, false);
    }

    if (takeLanes_)
        refreshTakeLanes();
}

// Loop region is now handled by MidiEditorContent::updateTimeRuler()

void PianoRollContent::updateGridLoopRegion() {
    if (draggingLoopRegion_) {
        gridComponent_->setLoopRegion(previewLoopStartBeats_, previewLoopLengthBeats_, true);
    }
}

void PianoRollContent::setGridPhasePreview(double beats, bool active) {
    gridComponent_->setPhasePreview(beats, active);
}

// ============================================================================
// Relative time mode (PianoRoll-specific multi-clip handling)
// ============================================================================

void PianoRollContent::setRelativeTimeMode(bool relative) {
    if (relativeTimeMode_ != relative) {
        relativeTimeMode_ = relative;

        // Reload clips based on new mode
        if (editingClipId_ != magda::INVALID_CLIP_ID) {
            auto& clipManager = magda::ClipManager::getInstance();
            auto& selectionManager = magda::SelectionManager::getInstance();
            const auto* clip = clipManager.getClip(editingClipId_);
            if (clip && clip->isMidi()) {
                magda::TrackId trackId = clip->trackId;

                // Get all selected clips
                const auto& selectedClipsSet = selectionManager.getSelectedClips();
                std::vector<magda::ClipId> selectedMidiClips;

                // Filter selected clips to only MIDI clips on this track
                for (magda::ClipId id : selectedClipsSet) {
                    auto* c = clipManager.getClip(id);
                    if (c && c->isMidi() && c->trackId == trackId) {
                        selectedMidiClips.push_back(id);
                    }
                }

                // If no selected clips or selected clips are on different track, use just the
                // primary
                if (selectedMidiClips.empty()) {
                    selectedMidiClips.push_back(editingClipId_);
                }

                if (relative) {
                    // Relative mode: show only selected clips
                    gridComponent_->setClips(trackId, selectedMidiClips, selectedMidiClips);
                } else {
                    // Absolute mode: show MIDI clips on this track matching the editing clip's view
                    auto allClipsOnTrack = clipManager.getClipsOnTrack(trackId, clip->view);

                    // Filter to MIDI clips only
                    std::vector<magda::ClipId> allMidiClips;
                    for (magda::ClipId id : allClipsOnTrack) {
                        auto* c = clipManager.getClip(id);
                        if (c && c->isMidi()) {
                            allMidiClips.push_back(id);
                        }
                    }

                    gridComponent_->setClips(trackId, selectedMidiClips, allMidiClips);
                }
            }
        }

        updateGridSize();  // Grid size changes between modes
        updateTimeRuler();
        updateVelocityLane();

        scrollToClipStartForTimeMode();
    }
}

void PianoRollContent::setChordRowVisible(bool visible) {
    if (showChordRow_ != visible) {
        showChordRow_ = visible;
        resized();
        repaint();
    }
}

// setVelocityDrawerVisible is now in the base class MidiEditorContent

// ============================================================================
// Activation
// ============================================================================

void PianoRollContent::onActivated() {
    installMidiNoteMonitor();

    magda::ClipId selectedClip = magda::ClipManager::getInstance().getSelectedClip();
    if (selectedClip != magda::INVALID_CLIP_ID) {
        const auto* clip = magda::ClipManager::getInstance().getClip(selectedClip);
        if (clip && clip->isMidi()) {
            editingClipId_ = selectedClip;
            gridComponent_->setClip(selectedClip);

            // Session clips and looping arrangement clips are locked to relative mode
            bool forceRelative = (clip->view == magda::ClipView::Session) || clip->loopEnabled;
            if (forceRelative) {
                setRelativeTimeMode(true);
            }

            // Auto-show chord row if track has a chord engine
            if (!showChordRow_ && clip->trackId != magda::INVALID_TRACK_ID) {
                auto* trackInfo = magda::TrackManager::getInstance().getTrack(clip->trackId);
                if (trackInfo) {
                    for (const auto& elem : trackInfo->chain.fxChainElements) {
                        if (magda::isDevice(elem)) {
                            const auto& dev = magda::getDevice(elem);
                            if (dev.pluginId.containsIgnoreCase(
                                    magda::daw::audio::MidiChordEnginePlugin::xmlTypeName)) {
                                setChordRowVisible(true);
                                chordToggle_->setActive(true);
                                break;
                            }
                        }
                    }
                }
            }

            updateGridSize();
            updateTimeRuler();
            updateVelocityLane();
            centerOnNotes();
        }
    }
    repaint();
}

void PianoRollContent::onDeactivated() {
    uninstallMidiNoteMonitor();
    if (keyboard_)
        keyboard_->clearPressedNotes();
}

// ============================================================================
// ClipManagerListener
// ============================================================================

void PianoRollContent::clipsChanged() {
    if (editingClipId_ != magda::INVALID_CLIP_ID) {
        auto& clipManager = magda::ClipManager::getInstance();
        const auto* clip = clipManager.getClip(editingClipId_);
        if (!clip) {
            gridComponent_->setClip(magda::INVALID_CLIP_ID);
            if (midiDrawer_)
                midiDrawer_->setClip(magda::INVALID_CLIP_ID);
            else if (velocityLane_)
                velocityLane_->setClip(magda::INVALID_CLIP_ID);
        } else {
            // Re-fetch all clips on this track (a split/delete may have changed the list)
            magda::TrackId trackId = clip->trackId;
            auto& selectionManager = magda::SelectionManager::getInstance();
            const auto& selectedClipsSet = selectionManager.getSelectedClips();

            std::vector<magda::ClipId> selectedMidiClips;
            for (magda::ClipId id : selectedClipsSet) {
                auto* c = clipManager.getClip(id);
                if (c && c->isMidi() && c->trackId == trackId) {
                    selectedMidiClips.push_back(id);
                }
            }
            if (selectedMidiClips.empty()) {
                selectedMidiClips.push_back(editingClipId_);
            }

            if (relativeTimeMode_) {
                gridComponent_->setClips(trackId, selectedMidiClips, selectedMidiClips);
            } else {
                // Only show clips matching the editing clip's view (arrangement or session)
                auto allClipsOnTrack = clipManager.getClipsOnTrack(trackId, clip->view);
                std::vector<magda::ClipId> allMidiClips;
                for (magda::ClipId id : allClipsOnTrack) {
                    auto* c = clipManager.getClip(id);
                    if (c && c->isMidi()) {
                        allMidiClips.push_back(id);
                    }
                }
                gridComponent_->setClips(trackId, selectedMidiClips, allMidiClips);
            }
        }
    }
    MidiEditorContent::clipsChanged();
    updateVelocityLane();

    // The comparison overlay reads the chord track's progression, which lives on
    // a different track than this editor — repaint so it refreshes when chords
    // are added, moved off the chord track, or cleared.
    if (showProgressionOverlay_)
        repaint();
}

void PianoRollContent::clipPropertyChanged(magda::ClipId clipId) {
    // A chord-track clip change won't be "displayed" here, but the comparison
    // overlay still needs to reflect it.
    if (showProgressionOverlay_)
        repaint();

    // Check if this clip is one of the displayed clips
    const auto& displayedClips = gridComponent_->getClipIds();
    bool isDisplayed = false;
    for (magda::ClipId id : displayedClips) {
        if (id == clipId) {
            isDisplayed = true;
            break;
        }
    }

    if (isDisplayed) {
        // Defer UI refresh asynchronously to prevent deleting components during event handling
        juce::Component::SafePointer<PianoRollContent> safeThis(this);
        juce::MessageManager::callAsync([safeThis, clipId]() {
            if (auto* self = safeThis.getComponent()) {
                // Re-evaluate force-relative mode (loop may have been toggled)
                const auto* clip = magda::ClipManager::getInstance().getClip(clipId);
                if (clip && clip->isMidi()) {
                    bool forceRelative =
                        (clip->view == magda::ClipView::Session) || clip->loopEnabled;
                    if (forceRelative) {
                        self->setRelativeTimeMode(true);
                    }
                }
                const bool placementMoved =
                    clip && !self->relativeTimeMode_ &&
                    (std::isnan(self->lastScrolledPlacementStartBeat_) ||
                     std::abs(clip->placement.startBeat - self->lastScrolledPlacementStartBeat_) >
                         0.0001);

                // Sync chord annotations with their linked notes
                self->syncChordAnnotations(clipId);

                // Auto-clear chord annotations if notes were all deleted
                if (clip && clip->midiNotes.empty() && !clip->chordAnnotations.empty()) {
                    magda::ClipManager::getInstance().clearChordAnnotations(clipId);
                }

                self->applyClipGridSettings();
                self->loadNoteHeightFromClip(self->editingClipId_);
                self->updateGridSize();
                // Relayout so the take-lanes strip appears/resizes when takes or
                // the takesExpanded toggle change.
                self->resized();
                self->updateTimeRuler();
                self->updateVelocityLane();
                if (placementMoved)
                    self->scrollToClipStartForTimeMode();
                self->repaint();
            }
        });
    }
}

void PianoRollContent::clipSelectionChanged(magda::ClipId clipId) {
    if (clipId == magda::INVALID_CLIP_ID) {
        // Selection cleared - clear the piano roll
        editingClipId_ = magda::INVALID_CLIP_ID;
        gridComponent_->setClip(magda::INVALID_CLIP_ID);
        if (keyboard_)
            keyboard_->clearPressedNotes();
        updateGridSize();
        updateTimeRuler();
        updateVelocityLane();
        repaint();
        return;
    }

    if (clipId != magda::INVALID_CLIP_ID) {
        auto& clipManager = magda::ClipManager::getInstance();
        auto& selectionManager = magda::SelectionManager::getInstance();
        const auto* clip = clipManager.getClip(clipId);
        if (clip && clip->isMidi()) {
            editingClipId_ = clipId;
            if (keyboard_)
                keyboard_->clearPressedNotes();
            loadNoteHeightFromClip(editingClipId_);

            magda::TrackId trackId = clip->trackId;

            // Get all selected clips
            const auto& selectedClipsSet = selectionManager.getSelectedClips();

            std::vector<magda::ClipId> selectedMidiClips;

            // Filter selected clips to only MIDI clips on this track
            for (magda::ClipId id : selectedClipsSet) {
                auto* c = clipManager.getClip(id);
                if (c && c->isMidi() && c->trackId == trackId) {
                    selectedMidiClips.push_back(id);
                }
            }

            // If no selected clips or selected clips are on different track, use just the primary
            if (selectedMidiClips.empty()) {
                selectedMidiClips.push_back(clipId);
            }

            if (relativeTimeMode_) {
                // Relative mode: show only selected clips
                gridComponent_->setClips(trackId, selectedMidiClips, selectedMidiClips);
            } else {
                // Absolute mode: show MIDI clips on this track matching the editing clip's view
                auto allClipsOnTrack = clipManager.getClipsOnTrack(trackId, clip->view);

                // Filter to MIDI clips only
                std::vector<magda::ClipId> allMidiClips;
                for (magda::ClipId id : allClipsOnTrack) {
                    auto* c = clipManager.getClip(id);
                    if (c && c->isMidi()) {
                        allMidiClips.push_back(id);
                    }
                }

                gridComponent_->setClips(trackId, selectedMidiClips, allMidiClips);
            }

            // Session clips are locked to relative mode
            bool forceRelative = (clip->view == magda::ClipView::Session);
            if (forceRelative) {
                setRelativeTimeMode(true);
            }

            updateGridSize();
            updateTimeRuler();
            updateVelocityLane();

            scrollToClipStartForTimeMode();
            centerOnNotes();

            repaint();
        }
    }
}

void PianoRollContent::clipDragPreview(magda::ClipId clipId, double previewStartTime,
                                       double previewLength) {
    // Only update if this is the clip we're editing
    if (clipId != editingClipId_) {
        return;
    }

    // Update TimeRuler with preview position in real-time
    timeRuler_->setTimeOffset(previewStartTime);
    timeRuler_->setClipLength(previewLength);

    // Also update the grid with preview clip boundaries
    double tempo = 120.0;
    if (auto* controller = magda::TimelineController::getCurrent()) {
        tempo = controller->getState().tempo.bpm;
    }
    double secondsPerBeat = 60.0 / tempo;
    double clipStartBeats = previewStartTime / secondsPerBeat;
    double clipLengthBeats = previewLength / secondsPerBeat;

    gridComponent_->setClipStartBeats(clipStartBeats);
    gridComponent_->setClipLengthBeats(clipLengthBeats);
}

// ============================================================================
// SelectionManagerListener
// ============================================================================

void PianoRollContent::selectionTypeChanged(magda::SelectionType /*newType*/) {
    // Selection type changed - refresh the view
    repaint();
}

void PianoRollContent::multiClipSelectionChanged(const std::unordered_set<magda::ClipId>& clipIds) {
    // Multi-clip selection changed - update piano roll to show selected clips
    if (clipIds.empty()) {
        return;
    }

    auto& clipManager = magda::ClipManager::getInstance();

    // Get the first clip to determine the track
    magda::ClipId firstClipId = *clipIds.begin();
    const auto* firstClip = clipManager.getClip(firstClipId);
    if (!firstClip || !firstClip->isMidi()) {
        return;
    }

    magda::TrackId trackId = firstClip->trackId;

    // Filter selected clips to only MIDI clips on this track
    std::vector<magda::ClipId> selectedMidiClips;
    for (magda::ClipId id : clipIds) {
        auto* c = clipManager.getClip(id);
        if (c && c->isMidi() && c->trackId == trackId) {
            selectedMidiClips.push_back(id);
        }
    }

    if (selectedMidiClips.empty()) {
        return;
    }

    // Update editing clip ID to the first selected clip
    editingClipId_ = selectedMidiClips[0];
    if (keyboard_)
        keyboard_->clearPressedNotes();

    // Session clips are locked to relative mode
    bool forceRelative = (firstClip->view == magda::ClipView::Session);
    if (forceRelative) {
        setRelativeTimeMode(true);
    }

    gridComponent_->setClips(trackId, selectedMidiClips, selectedMidiClips);

    updateGridSize();
    updateTimeRuler();
    updateVelocityLane();
    scrollToClipStartForTimeMode();
    centerOnNotes();
    repaint();
}

void PianoRollContent::noteSelectionChanged(const magda::NoteSelection& selection) {
    setVelocityLaneSelectedNotes(selection.noteIndices);
}

// ============================================================================
// Public Methods
// ============================================================================

void PianoRollContent::setClip(magda::ClipId clipId) {
    // Chord-focus editors always show the chord lane, independent of the
    // per-track chord-engine auto-show used by the standard piano roll.
    if (chordFocusMode())
        setChordRowVisible(true);

    if (editingClipId_ != clipId) {
        editingClipId_ = clipId;
        loadNoteHeightFromClip(editingClipId_);
        gridComponent_->setClip(clipId);
        if (keyboard_)
            keyboard_->clearPressedNotes();
        updateGridSize();
        updateTimeRuler();
        updateVelocityLane();

        scrollToClipStartForTimeMode();

        // Center vertically on existing notes (or C4 if empty)
        centerOnNotes();

        repaint();
    }
}

// ============================================================================
// Drawing helpers
// ============================================================================

void PianoRollContent::drawSidebar(juce::Graphics& g, juce::Rectangle<int> area) {
    // Draw sidebar background
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND_ALT));
    g.fillRect(area);

    // Draw right separator line
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawVerticalLine(area.getRight() - 1, static_cast<float>(area.getY()),
                       static_cast<float>(area.getBottom()));

    // Hairline dividers framing the icon clusters: the top tool group (chord /
    // fold / takes) and the bottom lane-toggle group (pitch-glide / CC /
    // velocity). Layout mirrors resized() so the lines sit in the gaps.
    const int iconSize = 22;
    const int padding = (sidebarWidth() - iconSize) / 2;
    const int chordToggleY =
        showChordRow_ ? chordRowTop() + (chordRowHeight() - iconSize) / 2 : padding;

    int topClusterBottom = chordToggleY + iconSize;  // chord only
    if (foldToggle_)
        topClusterBottom = chordToggleY + iconSize + padding + iconSize;
    if (takeLanesToggle_ && takeLanesToggle_->isVisible())
        topClusterBottom = chordToggleY + 2 * (iconSize + padding) + iconSize;

    const int bottomClusterTop = getHeight() - 3 * (iconSize + padding);
    const int topDividerY = topClusterBottom + padding / 2;
    const int bottomDividerY = bottomClusterTop - padding / 2;

    const float x1 = static_cast<float>(area.getX() + 5);
    const float x2 = static_cast<float>(area.getRight() - 5);
    if (bottomDividerY - topDividerY > padding) {
        g.drawHorizontalLine(topDividerY, x1, x2);
        g.drawHorizontalLine(bottomDividerY, x1, x2);
    }
}

void PianoRollContent::drawChordRow(juce::Graphics& g, juce::Rectangle<int> area) {
    // Draw chord row background
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND_ALT));
    g.fillRect(area);

    // Draw bottom border
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawLine(static_cast<float>(area.getX()), static_cast<float>(area.getBottom() - 1),
               static_cast<float>(area.getRight()), static_cast<float>(area.getBottom() - 1), 1.0f);

    // Get chord annotations from the editing clip
    const auto* clip = (editingClipId_ != magda::INVALID_CLIP_ID)
                           ? magda::ClipManager::getInstance().getClip(editingClipId_)
                           : nullptr;

    // Chord-block layout (#1504):
    //   chord-track chord -> LEFT, with the accent spine
    //   MIDI-track chord  -> RIGHT, no decoration (always the same spot)
    // On the chord track's own editor the blocks ARE chord-track chords (left +
    // spine). On a normal track the MIDI chord sits right/bare, and when the
    // overlay is on the intended chord-track chord is added left + spine. The
    // overlay's chord-track progression is drawn even where this track has no
    // chord of its own (the master-blocks pass after the own-chord loop).
    const bool isChordTrackLane =
        clip != nullptr && clip->trackId == magda::TrackManager::getInstance().getChordTrackId();
    const bool overlay = showProgressionOverlay_ && !isChordTrackLane;
    const auto master = overlay ? magda::ChordProgressionContext::current()
                                : std::vector<magda::ProgressionChord>{};

    const bool haveOwnChords = clip != nullptr && !clip->chordAnnotations.empty();
    if (!clip || (!haveOwnChords && master.empty())) {
        // Empty-state hint. The chord lane is populated by chord detection, so
        // say how to get chords here: the chord-track editor adds them on click;
        // a normal track detects them from its own notes via the scan button.
        g.setColour(DarkTheme::getSecondaryTextColour().withAlpha(0.4f));
        g.setFont(FontManager::getInstance().getUIFont(10.0f));
        const juce::String hint = chordFocusMode()
                                      ? "Click the lane to add a chord"
                                      : "No chords - press the scan button to detect from notes";
        g.drawText(hint, area.reduced(8, 0), juce::Justification::centredLeft, true);
        return;
    }

    int scrollX = viewport_ ? viewport_->getViewPositionX() : 0;
    g.setFont(FontManager::getInstance().getUIFont(11.0f));

    // Chord annotations use clip-relative beat positions (0, 4, 8...).
    // In absolute mode the viewport is scrolled to the clip's start, so we
    // must offset annotation positions by the clip's start beat.
    double clipStartBeats = 0.0;
    if (!relativeTimeMode_ && clip->view != magda::ClipView::Session) {
        clipStartBeats = clip->placement.startBeat;
    }

    // The accent spine takes its colour from the chord track (the chord that
    // owns the spine always belongs to the chord track, whether shown on its
    // own lane or as an overlay on a MIDI track).
    juce::Colour chordTrackColour = DarkTheme::getColour(DarkTheme::ACCENT_BLUE);
    if (auto* chordTrackInfo = magda::TrackManager::getInstance().getTrack(
            magda::TrackManager::getInstance().getChordTrackId()))
        chordTrackColour = chordTrackInfo->colour;
    auto intendedAt = [&](double absBeat) -> juce::String {
        for (const auto& mc : master)
            if (absBeat >= mc.startBeat - 0.01 && absBeat < mc.startBeat + mc.lengthBeats - 0.01)
                return mc.name;
        return {};
    };

    for (const auto& annotation : clip->chordAnnotations) {
        double absBeat = annotation.beatPosition + clipStartBeats;
        int startX = static_cast<int>(absBeat * horizontalZoom_) + GRID_LEFT_PADDING - scrollX;
        int endX = static_cast<int>((absBeat + annotation.lengthBeats) * horizontalZoom_) +
                   GRID_LEFT_PADDING - scrollX;

        // Skip if out of view
        if (endX < 0 || startX > area.getWidth())
            continue;

        // Clip to visible area
        int drawStartX = juce::jmax(0, startX) + area.getX();
        int drawEndX = juce::jmin(area.getWidth(), endX) + area.getX();

        // Draw chord block
        auto blockBounds = juce::Rectangle<int>(drawStartX + 1, area.getY() + 2,
                                                drawEndX - drawStartX - 2, area.getHeight() - 4);
        const bool selected =
            selectedChordGroup() != 0 && annotation.chordGroup == selectedChordGroup();
        const bool previewing =
            previewChordGroup() != 0 && annotation.chordGroup == previewChordGroup();
        const auto accent = DarkTheme::getColour(DarkTheme::ACCENT_BLUE);

        // The intended chord-track chord shown alongside this track's chord.
        // (Agreement/disagreement signalling is deferred — see follow-up issue.)
        const juce::String intendedName = overlay ? intendedAt(absBeat) : juce::String();
        const bool comparing = intendedName.isNotEmpty();

        // A playing block glows green; otherwise the accent-blue card.
        const auto fillColour =
            previewing ? DarkTheme::getColour(DarkTheme::STATUS_SUCCESS) : accent;
        const float fillAlpha = previewing ? 0.40f : selected ? 0.22f : 0.13f;

        // Slate card fill.
        g.setColour(fillColour.withAlpha(fillAlpha));
        g.fillRoundedRectangle(blockBounds.toFloat(), 4.0f);

        // The accent spine belongs to the chord-track chord (left). The MIDI
        // track's own chord is bare (no spine).
        const bool hasChordTrackChord = isChordTrackLane || comparing;
        if (hasChordTrackChord && blockBounds.getWidth() > 8) {
            juce::Rectangle<float> spine(static_cast<float>(blockBounds.getX() + 3),
                                         static_cast<float>(blockBounds.getY() + 3), 3.0f,
                                         static_cast<float>(blockBounds.getHeight() - 6));
            const auto spineColour = previewing ? fillColour : chordTrackColour;
            g.setColour(spineColour.withAlpha(previewing || selected ? 1.0f : 0.85f));
            g.fillRoundedRectangle(spine, 1.5f);
        }

        // Chord name(s) in the active notation (C / solfège / both).
        if (blockBounds.getWidth() > 14) {
            const auto& notation = magda::music::NotationSettings::getInstance();
            g.setFont(FontManager::getInstance().getUIFontMedium(13.0f));
            if (isChordTrackLane) {
                // Chord track's own chord: left, with spine.
                g.setColour(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
                g.drawText(notation.format(annotation.chordName),
                           blockBounds.withTrimmedLeft(12).withTrimmedRight(4),
                           juce::Justification::centredLeft, true);
            } else {
                // MIDI track chord: always right, no decoration.
                g.setColour(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
                g.drawText(notation.format(annotation.chordName),
                           blockBounds.withTrimmedLeft(8).withTrimmedRight(8),
                           juce::Justification::centredRight, true);
                // Intended chord-track chord (overlay only): left, with spine.
                if (comparing) {
                    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
                    g.drawText(notation.format(intendedName),
                               blockBounds.withTrimmedLeft(12).withTrimmedRight(4),
                               juce::Justification::centredLeft, true);
                }
            }
            g.setFont(FontManager::getInstance().getUIFont(11.0f));
        }

        // Selection ring + edge resize handles
        if (selected) {
            g.setColour(accent.withAlpha(0.9f));
            g.drawRoundedRectangle(blockBounds.toFloat().reduced(0.5f), 3.0f, 1.5f);

            constexpr int handleW = 3;
            auto lh = blockBounds.withWidth(handleW).reduced(0, 3);
            auto rh = lh.withX(blockBounds.getRight() - handleW);
            g.fillRoundedRectangle(lh.toFloat(), 1.5f);
            g.fillRoundedRectangle(rh.toFloat(), 1.5f);
        }
    }

    // Overlay: draw the chord-track progression as reference blocks (left +
    // spine, chord-track colour) so the chord track is visible even where this
    // track has no chord of its own. Master chords already shown alongside an
    // own chord (via intendedAt) are skipped to avoid drawing them twice.
    if (overlay) {
        const auto& notation = magda::music::NotationSettings::getInstance();
        for (const auto& mc : master) {
            bool covered = false;
            for (const auto& ann : clip->chordAnnotations) {
                const double annAbs = ann.beatPosition + clipStartBeats;
                if (mc.startBeat >= annAbs - 0.01 &&
                    mc.startBeat < annAbs + ann.lengthBeats - 0.01) {
                    covered = true;
                    break;
                }
            }
            if (covered)
                continue;

            int startX =
                static_cast<int>(mc.startBeat * horizontalZoom_) + GRID_LEFT_PADDING - scrollX;
            int endX = static_cast<int>((mc.startBeat + mc.lengthBeats) * horizontalZoom_) +
                       GRID_LEFT_PADDING - scrollX;
            if (endX < 0 || startX > area.getWidth())
                continue;

            int drawStartX = juce::jmax(0, startX) + area.getX();
            int drawEndX = juce::jmin(area.getWidth(), endX) + area.getX();
            auto blockBounds = juce::Rectangle<int>(
                drawStartX + 1, area.getY() + 2, drawEndX - drawStartX - 2, area.getHeight() - 4);
            if (blockBounds.getWidth() <= 0)
                continue;

            // Faint reference card + chord-track spine.
            g.setColour(chordTrackColour.withAlpha(0.10f));
            g.fillRoundedRectangle(blockBounds.toFloat(), 4.0f);
            if (blockBounds.getWidth() > 8) {
                juce::Rectangle<float> spine(static_cast<float>(blockBounds.getX() + 3),
                                             static_cast<float>(blockBounds.getY() + 3), 3.0f,
                                             static_cast<float>(blockBounds.getHeight() - 6));
                g.setColour(chordTrackColour.withAlpha(0.85f));
                g.fillRoundedRectangle(spine, 1.5f);
            }
            if (blockBounds.getWidth() > 14) {
                g.setFont(FontManager::getInstance().getUIFontMedium(13.0f));
                g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
                g.drawText(notation.format(mc.name),
                           blockBounds.withTrimmedLeft(12).withTrimmedRight(4),
                           juce::Justification::centredLeft, true);
                g.setFont(FontManager::getInstance().getUIFont(11.0f));
            }
        }
    }
}

void PianoRollContent::syncChordAnnotations(magda::ClipId clipId) {
    if (isSyncingChords_)
        return;
    isSyncingChords_ = true;

    auto* clip = magda::ClipManager::getInstance().getClip(clipId);
    if (!clip || clip->chordAnnotations.empty()) {
        isSyncingChords_ = false;
        return;
    }

    auto& engine = magda::music::ChordEngine::getInstance();

    for (auto it = clip->chordAnnotations.begin(); it != clip->chordAnnotations.end();) {
        if (it->chordGroup == 0) {
            ++it;
            continue;  // Skip unlinked annotations
        }

        // Find all notes in this chord group
        std::vector<magda::music::ChordNote> chordNotes;
        double minBeat = std::numeric_limits<double>::max();
        double maxEnd = 0.0;

        for (const auto& note : clip->midiNotes) {
            if (note.chordGroup == it->chordGroup) {
                chordNotes.push_back({note.noteNumber, note.velocity});
                minBeat = std::min(minBeat, note.startBeat);
                maxEnd = std::max(maxEnd, note.startBeat + note.lengthBeats);
            }
        }

        if (chordNotes.empty()) {
            // All notes in group deleted — remove annotation
            it = clip->chordAnnotations.erase(it);
            continue;
        }

        // Update position and length from note extents
        it->beatPosition = minBeat;
        it->lengthBeats = maxEnd - minBeat;

        // Re-detect chord name if pitches changed
        if (chordNotes.size() >= 2) {
            auto chord = engine.detect(chordNotes);
            if (chord.name != "none" && !chord.name.isEmpty())
                it->chordName = chord.getDisplayName();
        }
        ++it;
    }

    isSyncingChords_ = false;
}

void PianoRollContent::detectChordsFromNotes() {
    if (editingClipId_ == magda::INVALID_CLIP_ID)
        return;

    auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
    if (!clip || clip->midiNotes.empty())
        return;

    int beatsPerBar = magda::DEFAULT_TIME_SIGNATURE_NUMERATOR;
    if (auto* controller = magda::TimelineController::getCurrent())
        beatsPerBar = controller->getState().tempo.timeSignatureNumerator;

    // Bar-by-bar detection lives in the shared converter so this and the
    // "extract to chord track" feature stay in sync.
    const auto detected = magda::extractChordsFromNotes(clip->midiNotes, beatsPerBar);
    if (detected.empty())
        return;

    // Clear existing + add new, all as one undo step
    magda::CompoundOperationScope scope("Detect Chords");

    auto clearCmd = std::make_unique<magda::ClearChordAnnotationsCommand>(editingClipId_);
    magda::UndoManager::getInstance().executeCommand(std::move(clearCmd));

    // Assign chordGroup IDs to annotations and their notes
    std::vector<std::pair<size_t, int>> noteGroupAssignments;

    for (const auto& ex : detected) {
        int groupId = clip->nextChordGroupId++;

        magda::ClipInfo::ChordAnnotation annotation;
        annotation.beatPosition = ex.startBeat;
        annotation.lengthBeats = ex.lengthBeats;
        annotation.chordName = ex.name;
        annotation.chordGroup = groupId;

        auto cmd = std::make_unique<magda::AddChordAnnotationCommand>(editingClipId_, annotation);
        magda::UndoManager::getInstance().executeCommand(std::move(cmd));

        // Collect note-to-group assignments
        for (size_t noteIdx : ex.noteIndices)
            noteGroupAssignments.emplace_back(noteIdx, groupId);
    }

    // Tag notes with their chord group IDs (undoable)
    if (!noteGroupAssignments.empty()) {
        auto groupCmd = std::make_unique<magda::SetNoteChordGroupsCommand>(
            editingClipId_, std::move(noteGroupAssignments));
        magda::UndoManager::getInstance().executeCommand(std::move(groupCmd));
    }

    repaint();
}

void PianoRollContent::drawVelocityHeader(juce::Graphics& g, juce::Rectangle<int> area) {
    // Draw header background
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND_ALT));
    g.fillRect(area);

    // Draw top border
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawHorizontalLine(area.getY(), static_cast<float>(area.getX()),
                         static_cast<float>(area.getRight()));

    // Draw lane label in keyboard area (legacy path only, without MidiDrawer)
    auto labelArea = area.removeFromLeft(KEYBOARD_WIDTH);
    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    g.setFont(magda::FontManager::getInstance().getUIFont(11.0f));
    g.drawText("Velocity", labelArea.reduced(4, 0), juce::Justification::centredLeft, true);
}

void PianoRollContent::updateVelocityLane() {
    // Update the MIDI drawer if available
    if (midiDrawer_) {
        MidiEditorContent::updateMidiDrawer();

        // Pass multi-clip IDs for multi-clip velocity display
        if (gridComponent_) {
            midiDrawer_->setClipIds(gridComponent_->getSelectedClipIds());
        }

        // Override clip start beats for multi-clip mode
        const auto& selectedClipIds =
            gridComponent_ ? gridComponent_->getSelectedClipIds() : std::vector<magda::ClipId>{};
        if (selectedClipIds.size() > 1) {
            double earliestStart = std::numeric_limits<double>::max();
            auto& clipManager = magda::ClipManager::getInstance();
            for (magda::ClipId id : selectedClipIds) {
                const auto* c = clipManager.getClip(id);
                if (c) {
                    earliestStart = juce::jmin(earliestStart, c->placement.startBeat);
                }
            }
            if (earliestStart < std::numeric_limits<double>::max()) {
                midiDrawer_->setClipStartBeats(earliestStart);
            }
        }

        // Sync loop region and clip length
        if (gridComponent_) {
            midiDrawer_->setClipLengthBeats(gridComponent_->getClipLengthBeats());
            midiDrawer_->setLoopRegion(gridComponent_->getLoopOffsetBeats(),
                                       gridComponent_->getLoopLengthBeats(),
                                       gridComponent_->isLoopEnabled());
        }

        midiDrawer_->refreshAll();
        return;
    }

    // Fallback: legacy velocity-only path
    if (!velocityLane_)
        return;

    MidiEditorContent::updateVelocityLane();

    if (gridComponent_) {
        velocityLane_->setClipIds(gridComponent_->getSelectedClipIds());
    }

    const auto& selectedClipIds =
        gridComponent_ ? gridComponent_->getSelectedClipIds() : std::vector<magda::ClipId>{};
    if (selectedClipIds.size() > 1) {
        double earliestStart = std::numeric_limits<double>::max();
        auto& clipManager = magda::ClipManager::getInstance();
        for (magda::ClipId id : selectedClipIds) {
            const auto* c = clipManager.getClip(id);
            if (c) {
                earliestStart = juce::jmin(earliestStart, c->placement.startBeat);
            }
        }
        if (earliestStart < std::numeric_limits<double>::max()) {
            velocityLane_->setClipStartBeats(earliestStart);
        }
    }

    if (gridComponent_) {
        velocityLane_->setClipLengthBeats(gridComponent_->getClipLengthBeats());
        velocityLane_->setLoopRegion(gridComponent_->getLoopOffsetBeats(),
                                     gridComponent_->getLoopLengthBeats(),
                                     gridComponent_->isLoopEnabled());
    }

    velocityLane_->refreshNotes();
}

void PianoRollContent::onVelocityEdited() {
    // Refresh velocity lane (via base or drawer)
    MidiEditorContent::onVelocityEdited();
    if (midiDrawer_)
        midiDrawer_->refreshAll();
    // Also refresh grid component (PianoRoll-specific)
    if (gridComponent_) {
        gridComponent_->refreshNotes();
    }
}

void PianoRollContent::centerOnNote(int noteNumber) {
    if (!viewport_)
        return;

    int noteY = gridComponent_ ? gridComponent_->noteNumberToY(noteNumber)
                               : (MAX_NOTE - noteNumber) * noteHeight_;
    int viewportHeight = viewport_->getHeight();
    int scrollY = juce::jmax(0, noteY - (viewportHeight / 2) + (noteHeight_ / 2));

    viewport_->setViewPosition(viewport_->getViewPositionX(), scrollY);
    keyboard_->setScrollOffset(scrollY);
    if (octaveLabelStrip_)
        octaveLabelStrip_->setScrollOffset(scrollY);
}

void PianoRollContent::ensureNoteVisible(int noteNumber) {
    if (!viewport_ || noteHeight_ <= 0)
        return;

    const int noteTop = gridComponent_ ? gridComponent_->noteNumberToY(noteNumber)
                                       : (MAX_NOTE - noteNumber) * noteHeight_;
    const int noteBottom = noteTop + noteHeight_;
    const int viewTop = viewport_->getViewPositionY();
    const int viewHeight = viewport_->getHeight();
    const int viewBottom = viewTop + viewHeight;

    // Already fully visible — leave the view untouched.
    if (noteTop >= viewTop && noteBottom <= viewBottom)
        return;

    int newScrollY = viewTop;
    if (noteTop < viewTop)
        newScrollY = noteTop;  // off the top — bring flush to the top edge
    else
        newScrollY = noteBottom - viewHeight;  // off the bottom — flush to bottom edge

    newScrollY = juce::jmax(0, newScrollY);
    if (newScrollY == viewTop)
        return;

    viewport_->setViewPosition(viewport_->getViewPositionX(), newScrollY);
    keyboard_->setScrollOffset(newScrollY);
    if (octaveLabelStrip_)
        octaveLabelStrip_->setScrollOffset(newScrollY);
}

void PianoRollContent::centerOnNotes() {
    if (!viewport_)
        return;

    const auto* clip = magda::ClipManager::getInstance().getClip(editingClipId_);
    if (!clip || clip->midiNotes.empty()) {
        // No notes — default to C4 (MIDI note 72 in C-2 convention)
        centerOnNote(72);
        return;
    }

    // Find note range
    int minNote = 127;
    int maxNote = 0;
    for (const auto& note : clip->midiNotes) {
        minNote = juce::jmin(minNote, note.noteNumber);
        maxNote = juce::jmax(maxNote, note.noteNumber);
    }

    // Center on the midpoint of the note range
    int midNote = (minNote + maxNote) / 2;
    centerOnNote(midNote);
}

}  // namespace magda::daw::ui
